bool Sc6ReplayHost::ReadIndexObservation(const Sc6ReplayExecutor::Continuation& execution,
    ReplayTickIndex::Entry& row) noexcept
{
    ReplaySourceState source{};
    if (!ReadCheckpointSource(source).ok() || source.owner != index_source_.owner
        || source.recordings != index_source_.recordings || source.reset_images != index_source_.reset_images
        || source.recording_count != index_source_.recording_count || source.reset_count != index_source_.reset_count)
        return false;
    __try {
        row = {execution.tick, execution.interval, execution.publications,
            EngineField<std::uint32_t>(reinterpret_cast<void*>(image_base_), 0x470d0c4),
            EngineField<std::uint32_t>(manager_, 0x1490), source.round, source.cursor,
            execution.cache_offset, execution.remaining_inputs, static_cast<std::uint8_t>(execution.phase),
            EngineField<std::uint8_t>(manager_, 0x1480), source.tracker_active,
            EngineField<std::uint8_t>(manager_, 0x1463)};
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Sc6ReplayHost::IsRetainedSourceStopSafe() noexcept
{
    ReplayTickIndex::Entry row{};
    if(!CheckBinding() || !ReadIndexObservation(simulation_->continuation(),row))return false;
    __try {
        // Verified 1403F2840 includes manager overrides and rule consumers;
        // the world-mode fallback alone cannot establish this boundary.
        return !row.source_active && row.round_state==5 && index_source_.recording_count>0
            && row.round==index_source_.recording_count-1
            && EngineField<std::int32_t>(reinterpret_cast<void*>(image_base_),0x4846364)==5
            && !reinterpret_cast<std::uint8_t(*)(void*)>(image_base_+0x3f2840)(manager_);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

void Sc6ReplayHost::ObserveIndexBoundary(void* owner, const Sc6ReplayExecutor::Continuation& execution,
    bool interval_complete) noexcept
{
    auto* self = static_cast<Sc6ReplayHost*>(owner);
    if(!interval_complete && self->rolling_.schedule)self->ObserveRollingInput(execution);
    const auto observed = self->index_.witness();
    if (observed.phase != ReplayTickIndex::Phase::Recording) return;
    ReplayTickIndex::Entry row{};
    if (!self->ReadIndexObservation(execution, row)) {
        self->index_.Fail(FailureCode::GenerationMismatch); return;
    }
    if (!interval_complete) { self->index_.Append(row); return; }
    bool final_tail{};
    __try {
        // Native IsMatchFinished fallback plus the inactive final-round source.
        // The separate scene callback is still required before completion.
        final_tail = EngineField<std::int32_t>(reinterpret_cast<void*>(self->image_base_), 0x4846364) == 10
            && row.round_state == 10 && !row.source_active && row.round == self->index_source_.recording_count - 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { self->index_.Fail(FailureCode::ContextUnavailable); return; }
    self->index_.CompleteInterval(execution.interval, execution.tick, final_tail);
    if (final_tail && !observed.final_tail) RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] index endpoint observed tick={} interval={} world_mode=10 round_state=10 source_active=false\n"), execution.tick, execution.interval);
}

bool Sc6ReplayHost::BindIndexFinish()
{
    using namespace RC::Unreal;
    auto* function = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
        STR("/Game/UI/GameFlow/GameScenes/Battle/ReplayBattleScene.ReplayBattleScene_C:OnFinishMatch"));
    // UE4SS itself routes Blueprint hooks through ProcessInternal below4.22
    // (UObjectGlobals.cpp). SC6 reports4.17; its local-script detour is unavailable.
    // Admit the actual function binding, not a second speculative dispatch hook.
    if (!function || function->GetFunc() != UObject::ProcessInternalInternal.get_function_address()
        || function->HasAnyFunctionFlags(EFunctionFlags::FUNC_Native)) return false;
    const auto* item = FUObjectArray::IndexToObject(function->GetInternalIndex());
    if (!item || item->GetUObject() != function || !item->IsValid(false)) return false;
    index_finish_function_ = function;
    index_finish_function_index_ = function->GetInternalIndex();
    index_finish_function_serial_ = item->GetSerialNumber();
    // Global callback IDs can be removed even after the UFunction is collected.
    // No borrowed UFunction is dereferenced during retirement.
    const auto callback = [](auto&, UObject*, FFrame& stack, void*) {
        auto* self = active_;
        if (!self || GetCurrentThreadId() != self->thread_
            || (self->index_.witness().phase != ReplayTickIndex::Phase::Recording
                && self->index_.witness().phase != ReplayTickIndex::Phase::Complete)
            || stack.Node() != self->index_finish_function_) return;
        const auto* item = FUObjectArray::IndexToObject(self->index_finish_function_index_);
        if (!item || item->GetUObject() != self->index_finish_function_ || !item->IsValid(false)
            || item->GetSerialNumber() != self->index_finish_function_serial_) {
            self->index_.Fail(FailureCode::GenerationMismatch); return;
        }
        self->index_.ObserveNativeFinish();
        // Native callback returns normally: no Blueprint stack or result-menu
        // work is discarded. The application owner admits the subsequent hold.
        if(!self->SeekOwnsExecution() && !self->historical_restore_)
            self->index_end_hold_=IndexEndHold::AwaitingBoundary;
    };
    index_finish_hook_ = Hook::RegisterProcessInternalPostCallback(callback,
        {false, true, STR("HorseMod.ReplayIndex"), STR("NativeFinish")});
    return index_finish_hook_ != 0;
}

bool Sc6ReplayHost::UnbindIndex(bool retain_finish) noexcept
{
    if (index_.witness().phase == ReplayTickIndex::Phase::Empty && !index_finish_hook_) return true;
    if (simulation_ && !simulation_->SetBoundaryObserver(this, nullptr)) return false;
    if (retain_finish) return true; // Completed map still watches replayed endings.
    if (index_finish_hook_) {
        if (!RC::Unreal::Hook::UnregisterCallback(index_finish_hook_)) return false;
        index_finish_hook_ = 0;
    }
    index_finish_function_ = nullptr;
    index_end_hold_=IndexEndHold::Idle;index_end_tick_=0;
    return true;
}

bool Sc6ReplayHost::IndexOperation(IndexAction action, ReplayTickIndex::Witness* output, std::size_t budget) noexcept
{
    if (!output) return false;
    auto* self = active_;
    if (!self || GetCurrentThreadId() != self->thread_) {
        *output = {ReplayTickIndex::Phase::Failed, self ? FailureCode::WrongThread : FailureCode::ContextUnavailable};
        return false;
    }
    const auto publish = [&](bool success) { *output = self->index_.witness(); return success; };
    if (action == IndexAction::Read) return publish(true);
    if (action == IndexAction::Cancel) return publish(self->index_.Cancel());
    if (self->depth_ || !self->simulation_->interval_complete()) return publish(false);
    if(action==IndexAction::RetainCapture) {
        // Capture completes at a real application boundary. Its tick must
        // already exist in this source's map; retention never invents entries
        // or turns partial indexing into a completed seekable timeline.
        if(self->index_.witness().phase!=ReplayTickIndex::Phase::Recording
            || self->capture_operation_.phase!=CapturePhase::Ready || !self->captured_checkpoint_
            || self->captured_checkpoint_->execution.tick>=self->index_.witness().entries)
            return publish(false);
        const auto* image=self->captured_checkpoint_.get();
        for(unsigned scene=0;scene<image->physics.broadphases.size();++scene) {
            const auto& physics=image->physics.broadphases[scene];
            if(physics.scene && !physics.valid) {
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] index checkpoint intrinsic rejection tick={} scene={} check={} B_captured=false A_published=false\n"),
                    image->execution.tick,scene,RC::to_generic_string(ReplayPhysicsSapImage::AdmissionName(physics.admission_step)));
                self->index_.Fail(FailureCode::UnsupportedContent);return publish(false);
            }
        }
        std::size_t count{};
        const auto owned=OwnCheckpoint(CheckpointOwnership::Retain,image,&count);
        if(!owned.ok()) {
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] index retention rejected tick={} code={} depth={} binding={} retirement_safe={} target_shape={} hud_limitation={} owned_bytes={}\n"),
                image->execution.tick,static_cast<unsigned>(owned.code),self->depth_,self->CheckBinding(),self->CanRetireSeekCheckpoint(),
                image->historical_target_shape_supported(),RC::to_generic_string(image->hud ? (image->hud->historical_target_limitation()?image->hud->historical_target_limitation():"none") : "missing"),self->AdmissionBytes());
            self->index_.Fail(owned.code);return publish(false);
        }
        CheckpointHandle transferred;CaptureWitness capture{};
        if(!CaptureOperation(CaptureAction::Take,&capture,&transferred)) {
            OwnCheckpoint(CheckpointOwnership::Release,image,&count);
            self->index_.Fail(FailureCode::CaptureFailed);return publish(false);
        }
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] index checkpoint owned tick={} retained={} owned_bytes={}\n"),image->execution.tick,count,self->AdmissionBytes());
        return publish(true); // The host pin survives this temporary handle.
    }
    if (action == IndexAction::Release) {
        if (self->index_.witness().phase == ReplayTickIndex::Phase::Recording || !self->UnbindIndex()) return publish(false);
        // Engine-post callers still owe the application tail. Accept intent
        // here; the application owner retires CPU leases and render references.
        self->index_.ReleaseAfterOwners(self->checkpoint_pins_.empty() && !self->seek_retirement_pending_
            && !Sc6ReplayParticleCopy::capture_retirement_pending());
        return publish(true); // Accepted release; Releasing is not completion.
    }
    if ((action != IndexAction::Begin && action != IndexAction::BeginManaged) || self->SessionExitRequested()
        || self->index_.witness().phase != ReplayTickIndex::Phase::Empty
        || self->SeekOwnsExecution() || self->historical_restore_ || self->simulation_->continuation().tick
        || !self->CheckBinding() || budget > self->AdmissionRemaining()
        || !self->ReadCheckpointSource(self->index_source_).ok()) return publish(false);
    ReplayTickIndex::Entry baseline{};
    if (!self->ReadIndexObservation(self->simulation_->continuation(), baseline)) return publish(false);
    auto status = self->index_.Begin(baseline, budget,self->input_source_->revision_id(),
        action==IndexAction::BeginManaged?ReplayTickIndex::EndPolicy::RetainedSourceStop:ReplayTickIndex::EndPolicy::NativeFinish);
    if (!status.ok()) { *output = {ReplayTickIndex::Phase::Failed, status.code}; return false; }
    try {
        if (self->BindSessionExit() && self->BindIndexFinish() && self->simulation_->SetBoundaryObserver(self, &ObserveIndexBoundary)) {
            self->index_checkpoint_selection_=action==IndexAction::BeginManaged
                ?IndexCheckpointSelection::FirstCombat:IndexCheckpointSelection::Explicit;
            self->index_checkpoint_round_=0;
            self->index_checkpoint_replacement_attempts_=0;
            Horse::GameImGui::PresentHook::instance().take_replay_index_cancel();
            Horse::GameImGui::set_visible(true);
            return publish(true);
        }
    } catch (...) {}
    self->index_.Fail(FailureCode::ContextUnavailable);
    self->UnbindIndex(); // A failed removal keeps its ID/owner for later retirement.
    return publish(false);
}

bool Sc6ReplayHost::ReadIndexEntry(std::uint64_t tick, ReplayTickIndex::Entry* output) noexcept
{
    auto* self = active_;
    if (!self || !output || GetCurrentThreadId() != self->thread_) return false;
    const auto phase=self->index_.witness().phase;
    if(phase!=ReplayTickIndex::Phase::Recording && phase!=ReplayTickIndex::Phase::Complete) return false;
    const auto rows = self->index_.entries();
    if (tick >= rows.size()) return false;
    *output = rows[static_cast<std::size_t>(tick)]; return true;
}

void Sc6ReplayHost::ServiceIndexControls() noexcept
{
    auto& ui=Horse::GameImGui::PresentHook::instance();
    const bool playback_boundary=capture_operation_.phase==CapturePhase::Idle
        && (interior_phase_==InteriorPhase::Idle || interior_phase_==InteriorPhase::Resumed);
    const auto current=simulation_->continuation().tick;
    const bool can_pause=index_.witness().phase==ReplayTickIndex::Phase::Complete
        && current<index_.witness().entries-1 && playback_boundary && !SessionExitRequested()
        && !SeekOwnsExecution() && !historical_restore_ && !checkpoint_restoring_
        && !surface_event_ && !particle_command_pending_.load() && !seek_retirement_pending_;
    ui.publish_replay_pause_availability(can_pause,current);
    if(ui.take_replay_pause_request()) {
        const bool accepted=can_pause && ArmPause(current+1,nullptr,nullptr,PauseBoundary::NextCompletedApplication);
        ui.report_replay_pause_request(accepted);
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] playback pause request origin={} accepted={} boundary=next_completed_application\n"),current,accepted);
    }
    const bool cancel_owner=index_checkpoint_.pending() || playback_boundary;
    if(index_.witness().phase==ReplayTickIndex::Phase::Recording && cancel_owner && ui.take_replay_index_cancel()) {
        index_.Cancel();
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] index UI cancellation accepted tick={} checkpoint_pending={}\n"),
            simulation_->continuation().tick,index_checkpoint_.pending());
    }
    // A checkpoint in progress owns its own hold and retirement. During
    // playback the existing application-boundary retirement owner blocks the
    // next engine epoch until the last checkpoint references have retired.
    if(index_.witness().phase==ReplayTickIndex::Phase::Cancelled && !index_checkpoint_.pending() && playback_boundary) {
        ReplayTickIndex::Witness released{};
        if(!IndexOperation(IndexAction::Release,&released,0))index_.Fail(FailureCode::PresentationFailed);
    }
    const auto state=index_.witness();
    const unsigned phase=state.phase==ReplayTickIndex::Phase::Recording?1u:
        state.phase==ReplayTickIndex::Phase::Cancelled || state.phase==ReplayTickIndex::Phase::Releasing
            || (state.phase==ReplayTickIndex::Phase::Failed && index_checkpoint_.pending())?2u:
        state.phase==ReplayTickIndex::Phase::Failed?3u:0u;
    ui.publish_replay_index_progress(phase,state.entries?state.entries-1:0,static_cast<unsigned>(state.failure),
        state.phase==ReplayTickIndex::Phase::Recording && cancel_owner);
    if(!phase)ui.take_replay_index_cancel(); // Never carry a stale click into a new session.
}

void Sc6ReplayHost::FinishIndexAtApplicationBoundary() noexcept
{
    ServiceIndexControls();
    FinishIndexCheckpointResume();
    if(index_.witness().phase==ReplayTickIndex::Phase::Recording
        && (seek_retirement_pending_ || seek_retirement_failed_)) {
        if(seek_retirement_failed_)index_.Fail(FailureCode::PresentationFailed);
        return;
    }
    if(index_.witness().phase==ReplayTickIndex::Phase::Recording
        && !index_checkpoint_.pending() && capture_operation_.phase==CapturePhase::Idle
        && !SeekOwnsExecution() && !historical_restore_ && !checkpoint_restoring_
        && CanRetireSeekCheckpoint() && !surface_event_ && !particle_command_pending_.load()
        && !seek_retirement_pending_ && !seek_retirement_failed_ && !SessionExitRequested()) {
        const auto status=RetireRejectedIndexCheckpoints();
        if(!status.ok()){index_.Fail(status.code);return;}
        if(seek_retirement_pending_)return; // Native retirement before another epoch/capture.
    }
    SelectInitialIndexCheckpoint();
    const auto state = index_.witness();
    if(state.end_policy==ReplayTickIndex::EndPolicy::RetainedSourceStop
        && (state.phase==ReplayTickIndex::Phase::Recording || state.phase==ReplayTickIndex::Phase::Complete)
        && !SeekOwnsExecution() && !historical_restore_ && capture_operation_.phase==CapturePhase::Idle
        && application_phase_==ApplicationPhase::Idle && engine_idle() && executor_.idle()
        && simulation_->interval_complete()) {
        const auto tick=simulation_->continuation().tick;
        const bool recording=state.phase==ReplayTickIndex::Phase::Recording;
        const bool eligible=interior_phase_==InteriorPhase::Idle || interior_phase_==InteriorPhase::Resumed;
        if(eligible && IsRetainedSourceStopSafe()) {
            if(state.native_finish || state.final_tail || tick==UINT64_MAX
                || (!recording && tick+1!=state.entries-1)
                || (recording && !index_.ObserveRetainedSourceStop(tick).ok())
                || !ArmPause(tick+1,nullptr,nullptr,PauseBoundary::CompletedApplication)) {
                index_.Fail(FailureCode::ContextUnavailable);interior_phase_=InteriorPhase::Failed;return;
            }
            index_end_tick_=tick+1;index_end_hold_=IndexEndHold::Arming;
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] retained source stop armed source_tick={} target={} native_finish=false unsupported_native_tail=true\n"),tick,index_end_tick_);
        }
        if(index_end_hold_==IndexEndHold::Arming && interior_phase_==InteriorPhase::Holding
            && pause_boundary_==PauseBoundary::CompletedApplication && !surface_event_ && held_surface_ && held_surface_->retained) {
            if(tick!=index_end_tick_ || !IsRetainedSourceStopSafe() || index_.witness().native_finish
                || (recording && !index_.FinishRetainedSourceStop(tick).ok())) {
                index_.Fail(FailureCode::GenerationMismatch);interior_phase_=InteriorPhase::Failed;return;
            }
            index_end_hold_=IndexEndHold::Held;
            const auto result=index_.witness();
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] retained source stop complete source_tick={} last_tick={} entries={} intervals={} empty={} multi={} bytes={} native_finish=false application_idle=true bindings_valid=true unsupported_native_tail=true\n"),
                index_.source_end_tick(),tick,result.entries,result.completed_intervals,result.zero_tick_intervals,result.multi_tick_intervals,result.bytes);
        }
    }
    if(state.phase==ReplayTickIndex::Phase::Releasing && CanRetireSeekCheckpoint()) {
        if(seek_retirement_failed_) index_.Fail(FailureCode::PresentationFailed);
        else {
            if(!checkpoint_pins_.empty()) {
                for(const auto& image:checkpoint_pins_)
                    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] index checkpoint release witness tick={} current_tick={} vfx_owners_live={} target_shape_supported={} owned_bytes={}\n"),
                        image->execution.tick,simulation_->continuation().tick,image->vfx.retained_owners_live(),image->historical_target_shape_supported(),AdmissionBytes());
                std::size_t retained{};
                const auto status=OwnCheckpoint(CheckpointOwnership::ReleaseAll,nullptr,&retained);
                if(!status.ok()) index_.Fail(status.code);
            }
            if(index_.witness().phase==ReplayTickIndex::Phase::Releasing && !seek_retirement_pending_
                && !Sc6ReplayParticleCopy::capture_retirement_pending()) index_.ReleaseAfterOwners(true);
        }
    }
    if(index_end_hold_==IndexEndHold::AwaitingBoundary && state.native_finish && state.final_tail
        && (state.phase==ReplayTickIndex::Phase::Recording || state.phase==ReplayTickIndex::Phase::Complete)
        && !SeekOwnsExecution() && !historical_restore_ && capture_operation_.phase==CapturePhase::Idle
        && application_phase_==ApplicationPhase::Idle && engine_idle() && executor_.idle()
        && simulation_->interval_complete()) {
        // Use the existing completed-application pause path. One further native
        // traversal supplies the final display, just as the qualified endpoint
        // caller did. Continue indexing every traversal through this boundary;
        // source completion remains a separately reported coordinate.
        const auto tick=simulation_->continuation().tick;
        ReplayTickIndex::Entry current{};
        const bool admitted=tick!=UINT64_MAX && ReadIndexObservation(simulation_->continuation(),current)
            && EngineField<std::int32_t>(reinterpret_cast<void*>(image_base_),0x4846364)==10
            && current.round_state==10 && !current.source_active
            && current.round==index_source_.recording_count-1
            && ArmPause(tick+1,nullptr,nullptr,PauseBoundary::CompletedApplication);
        if(!admitted) {
            index_.Fail(FailureCode::ContextUnavailable);
            interior_phase_=InteriorPhase::Failed;
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] replay end hold rejected tick={} reload_required=true\n"),tick);
            return;
        }
        index_end_tick_=tick+1;index_end_hold_=IndexEndHold::Arming;
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] replay end hold armed replay_tick={} native_target={} native_finish_returned=true application_tail_complete=true\n"),
            index_.source_end_tick(),index_end_tick_);
    }
    if(index_end_hold_==IndexEndHold::Arming && interior_phase_==InteriorPhase::Holding
        && pause_boundary_==PauseBoundary::CompletedApplication && !surface_event_ && held_surface_ && held_surface_->retained) {
        if(simulation_->continuation().tick!=index_end_tick_ || !CheckBinding()) {
            interior_phase_=InteriorPhase::Failed;return;
        }
        index_end_hold_=IndexEndHold::Held;
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] replay end retained replay_tick={} native_tick={} completed_application=true native_bindings_valid=true surface_retained=true\n"),
            index_.source_end_tick(),index_end_tick_);
    }
    if (state.phase == ReplayTickIndex::Phase::Recording && state.native_finish && state.final_tail
        && application_phase_ == ApplicationPhase::Idle && engine_idle() && executor_.idle()
        && simulation_->interval_complete() && index_end_hold_==IndexEndHold::Held) {
        // All native ticks, including finish notification/application work,
        // must now have entries before the timeline can become seekable.
        const auto endpoint = state.entries - 1;
        const auto physical_tick = simulation_->continuation().tick;
        if(!index_.FinishAtApplicationBoundary(physical_tick).ok()) {
            interior_phase_=InteriorPhase::Failed;return;
        }
        const auto result = index_.witness();
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] replay index final phase={} entries={} intervals={} empty={} multi={} bytes={} native_finish=true final_tail=true\n"),
            static_cast<unsigned>(result.phase), result.entries, result.completed_intervals,
            result.zero_tick_intervals, result.multi_tick_intervals, result.bytes);
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] index closure replay_tick={} executor_tick={} application_idle=true\n"),index_.source_end_tick(),physical_tick);
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] index retained coverage source_tick={} last_tick={} tail_ticks={} entries={} all_native_ticks_indexed=true\n"),
            index_.source_end_tick(),endpoint,endpoint-index_.source_end_tick(),result.entries);
    }
    if (index_.witness().phase != ReplayTickIndex::Phase::Recording && simulation_->interval_complete())
        UnbindIndex(index_.witness().phase==ReplayTickIndex::Phase::Complete);
}
