void Sc6ReplayHost::TraceSeekCancellation(std::uint64_t tick,bool accepted) const noexcept
{
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] replay UI cancel dispatched tick={} accepted={} phase={}\n"),
        tick,accepted,static_cast<unsigned>(seek_.witness.phase));
}

void Sc6ReplayHost::TraceIndexedSeek(std::uint64_t target,bool accepted,std::uint64_t checkpoint) const noexcept
{
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] indexed seek dispatched target={} accepted={} checkpoint={} failure={}\n"),
        target,accepted,checkpoint,static_cast<unsigned>(indexed_seek_failure_));
}

void Sc6ReplayHost::TraceIndexedSeekAlreadyHeld(std::uint64_t tick) const noexcept
{
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] indexed seek already held tick={}\n"),tick);
}

void Sc6ReplayHost::TraceSeekAdvanceFailure(std::uint64_t tick) const noexcept
{
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] seek advance failure injected tick={} target={} B_retained={} external_hold=true\n"),
        tick,seek_.witness.target,historical_restore_ && historical_restore_->execution
            && historical_restore_->execution->ownership.retains_undo());
}

bool Sc6ReplayHost::ArmInteriorPause(std::uint64_t tick, void* context, InteriorObserver observer) noexcept
{
    return ArmPause(tick, context, observer, PauseBoundary::SimulationTick);
}

Status Sc6ReplayHost::ReviseInputs(std::span<const ReplayInputOverride> edits,std::uint64_t expected,std::uint64_t& revision) noexcept
{
    revision=0;
    if(index_.witness().phase==ReplayTickIndex::Phase::Recording)
        return Status::failure(FailureCode::IllegalTransition);
    if(SeekOwnsExecution() && !seek_driving_ && seek_.witness.phase!=SeekPhase::Restored)
        return Status::failure(FailureCode::IllegalTransition);
    if(active_!=this || GetCurrentThreadId()!=thread_) return Status::failure(FailureCode::WrongThread);
    const bool seek_revision=seek_.witness.phase==SeekPhase::Restored && HistoricalExecutionAdmitted();
    if(capture_operation_.phase!=CapturePhase::Idle || !CheckBinding() || depth_ || !input_source_
        || ((checkpoint_restoring_ || historical_restore_) && !seek_revision)
        || interior_phase_!=InteriorPhase::Holding || pause_boundary_!=PauseBoundary::CompletedApplication
        || application_phase_!=ApplicationPhase::Idle || !engine_idle() || !executor_.idle()
        || !executor_.arena_empty() || !simulation_->interval_complete())
        return Status::failure(FailureCode::IllegalTransition);
    ReplaySourceState source{};
    const auto status=ReadCheckpointSource(source);
    if(!status.ok()) return status;
    auto* log=EngineField<void*>(manager_,0x478);
    if(!log || EngineField<int>(log,0x390)!=0 || EngineField<int>(log,0x3a0)!=source.round
        || EngineField<int>(log,0x3a4)!=source.cursor)
        return Status::failure(FailureCode::UnsupportedContent);
    const auto revised=input_source_->Revise(source,expected,edits,AdmissionRemaining(),revision);
    if(revised.ok() && revision!=expected) {
        if(historical_restore_) index_.SuspendSourceRevision(revision);
        else index_.SettleSourceRevision(revision);
    }
    return revised;
}

bool Sc6ReplayHost::SetPauseMonitor(void* context,PauseMonitor monitor) noexcept
{
    auto* self=active_;
    if(!self || GetCurrentThreadId()!=self->thread_ || self->depth_) return false;
    const bool pending_end=(self->index_.witness().phase==ReplayTickIndex::Phase::Recording
            || self->index_.witness().phase==ReplayTickIndex::Phase::Complete)
        && self->index_end_hold_==IndexEndHold::Arming
        && (self->interior_phase_==InteriorPhase::Arming || self->interior_phase_==InteriorPhase::Armed);
    const bool pending_checkpoint=self->index_checkpoint_.phase==IndexCheckpointPhase::AwaitingBoundary
        && self->index_.witness().phase==ReplayTickIndex::Phase::Recording
        && (self->interior_phase_==InteriorPhase::Arming || self->interior_phase_==InteriorPhase::Armed);
    if(monitor && ((self->interior_phase_!=InteriorPhase::Holding && !pending_end && !pending_checkpoint) || self->pause_monitor_)) return false;
    self->pause_monitor_=monitor;self->pause_monitor_context_=monitor?context:nullptr;
    return true;
}

Sc6ReplayHost::TickAdvanceWitness Sc6ReplayHost::ReadTickAdvance() const noexcept
{
    if (active_ != this || GetCurrentThreadId() != thread_)
        return {TickAdvancePhase::Failed, FailureCode::WrongThread};
    return tick_advance_;
}

Sc6ReplayHost::TickAdvanceWitness Sc6ReplayHost::CompleteApplicationToHold(void* context,InteriorObserver observer) noexcept
{
    auto* self=active_;
    if(!self || GetCurrentThreadId()!=self->thread_)
        return {TickAdvancePhase::Failed,FailureCode::WrongThread};
    if(self->SeekOwnsExecution() && !self->seek_driving_)
        return {TickAdvancePhase::Failed,FailureCode::IllegalTransition};
    if(self->tick_advance_.phase==TickAdvancePhase::Settling) return self->tick_advance_;
    const bool owned=self->HistoricalExecutionAdmitted();
    const bool recovering=owned && self->historical_restore_->execution->ownership.phase()==ReplaySeekOwnership::Phase::RecoveryQuiescing;
    if(self->tick_advance_.phase!=TickAdvancePhase::Idle && self->tick_advance_.phase!=TickAdvancePhase::Held
        && self->tick_advance_.phase!=TickAdvancePhase::CompletionBlocked
        && !(recovering && self->tick_advance_.phase==TickAdvancePhase::Failed))
        return {TickAdvancePhase::Failed,FailureCode::IllegalTransition};
    if(self->capture_operation_.phase!=CapturePhase::Idle || self->depth_ || !self->CheckBinding()
        || ((self->historical_restore_ || self->checkpoint_restoring_) && !owned)
        || self->interior_phase_!=InteriorPhase::Holding || self->pause_boundary_!=PauseBoundary::SimulationTick
        || self->engine_phase_!=EnginePhase::World || !self->engine_post_deferred_
        || self->application_phase_!=ApplicationPhase::Engine || self->executor_.idle()
        || !self->executor_.task_groups().pending_manager_task()
        || (self->surface_event_ && self->surface_command_!=SurfaceCommand::Draw)
        || self->particle_command_pending_.load() || (self->particle_copy_ && self->particle_copy_->blocks_resume() && !owned))
        return {TickAdvancePhase::Failed,FailureCode::IllegalTransition};
    self->tick_advance_={TickAdvancePhase::Settling,FailureCode::None,self->simulation_->continuation().tick,0};
    self->tick_advance_context_=context;self->tick_advance_observer_=observer;
    self->interior_resume_requested_=false;
    return self->tick_advance_;
}

Sc6ReplayHost::TickAdvanceWitness Sc6ReplayHost::AdvanceToTick(std::uint64_t target,
    void* context, InteriorObserver observer) noexcept
{
    const auto reject = [&](FailureCode code) {
        if(GetCurrentThreadId()==thread_) RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] tick request rejected target={} code={} phase={} depth={} surface_pending={} surface_command={} particle_pending={} historical={} restoring={} deferred_post={}\n"),
            target,static_cast<unsigned>(code),static_cast<unsigned>(interior_phase_),depth_,surface_event_!=nullptr,
            static_cast<unsigned>(surface_command_),particle_command_pending_.load(),historical_restore_!=nullptr,checkpoint_restoring_,engine_post_deferred_);
        return TickAdvanceWitness{TickAdvancePhase::Failed, code, 0, target};
    };
    if (active_ != this || GetCurrentThreadId() != thread_) return reject(FailureCode::WrongThread);
    if(index_.witness().phase==ReplayTickIndex::Phase::Complete && target>=index_.witness().entries)
        return reject(FailureCode::InvalidConfiguration);
    if(SeekOwnsExecution() && !seek_driving_) return reject(FailureCode::IllegalTransition);
    if (tick_advance_.phase==TickAdvancePhase::Settling) return reject(FailureCode::IllegalTransition);
    if (tick_advance_.phase == TickAdvancePhase::Releasing || tick_advance_.phase == TickAdvancePhase::Arming
        || tick_advance_.phase == TickAdvancePhase::Advancing || tick_advance_.phase == TickAdvancePhase::Stepping)
        return target == tick_advance_.target ? tick_advance_ : reject(FailureCode::IllegalTransition);
    const bool owned=seek_driving_ && HistoricalExecutionAdmitted();
    if (capture_operation_.phase!=CapturePhase::Idle || depth_ || !CheckBinding()
        || ((checkpoint_restoring_ || historical_restore_) && !owned)
        || interior_phase_ != InteriorPhase::Holding
        || (surface_event_ && !(pause_boundary_ == PauseBoundary::SimulationTick && surface_command_ == SurfaceCommand::Draw))
        || particle_command_pending_.load()
        || (particle_copy_ && particle_copy_->blocks_resume() && !owned)) return reject(FailureCode::IllegalTransition);
    const auto origin = simulation_->continuation().tick;
    if (target < origin) return reject(FailureCode::MissingSnapshot);
    if (target == origin) {
        tick_advance_ = {TickAdvancePhase::Held, FailureCode::None, origin, target};
        return tick_advance_;
    }
    if (pause_boundary_ == PauseBoundary::SimulationTick) {
        if (engine_phase_ != EnginePhase::World || !engine_post_deferred_
            || !executor_.task_groups().pending_manager_task() || executor_.idle()
            || !Horse::GameImGui::PresentHook::instance().replay_surface_ready())
            return reject(FailureCode::UnsupportedContent);
        // Leave the current observer call before advancing. The next complete
        // application invocation owns the step and its new external hold.
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] interior step queued origin={} target={} pending_draw={}\n"),origin,target,surface_event_!=nullptr);
        tick_advance_ = {TickAdvancePhase::Stepping, FailureCode::None, origin, target};
        tick_advance_context_ = context; tick_advance_observer_ = observer;
        interior_resume_requested_ = false;
        return tick_advance_;
    }
    // Restoring expired pending tasks is a separate, unsupported admission.
    // This path starts with all previous application work already completed.
    if (pause_boundary_ != PauseBoundary::CompletedApplication || !engine_idle()
        || application_phase_ != ApplicationPhase::Idle || !executor_.idle()
        || !executor_.arena_empty() || !simulation_->interval_complete()
        || executor_.task_groups().pending_manager_task()) return reject(FailureCode::UnsupportedContent);
    tick_advance_ = {TickAdvancePhase::Releasing, FailureCode::None, origin, target};
    tick_advance_context_ = context;
    tick_advance_observer_ = observer;
    interior_resume_requested_ = true;
    return tick_advance_;
}

bool Sc6ReplayHost::ArmApplicationPause(std::uint64_t tick, void* context, InteriorObserver observer) noexcept
{
    return ArmPause(tick, context, observer, PauseBoundary::CompletedApplication);
}

bool Sc6ReplayHost::ArmPause(std::uint64_t tick, void* context, InteriorObserver observer, PauseBoundary boundary) noexcept
{
    const auto rejected=[&](unsigned stage) {
        RC::Output::send<RC::LogLevel::Warning>(STR(
            "[HorseMod] pause arm rejected stage={} target={} current={} thread={} depth={} engine_idle={} "
            "yield={} capture={} interior={} application={}\n"),stage,tick,
            simulation_?simulation_->continuation().tick:UINT64_MAX,GetCurrentThreadId()==thread_,depth_,engine_idle(),
            yield_each_tick_,static_cast<unsigned>(capture_operation_.phase),static_cast<unsigned>(interior_phase_),
            static_cast<unsigned>(application_phase_));
        return false;
    };
    // The initial baseline is reached before the first owned application.
    // Its predecessor is the native application in which the host attached;
    // suspension occurs only at the next application entry, after that tail.
    const bool initial_boundary=boundary==PauseBoundary::CompletedApplication && tick==0 && simulation_
        && simulation_->continuation().tick==0 && simulation_->continuation().interval==0
        && completed_applications_==0 && completed_engines_==0 && !engine_post_deferred_
        && application_phase_==ApplicationPhase::Idle && interior_phase_==InteriorPhase::Idle;
    if (active_ != this || GetCurrentThreadId() != thread_ || capture_operation_.phase!=CapturePhase::Idle || depth_ || !engine_idle()
        || !yield_each_tick_ || !simulation_ || !CheckBinding()
        || (interior_phase_ != InteriorPhase::Idle && interior_phase_ != InteriorPhase::Resumed)
        || (tick <= simulation_->continuation().tick && !initial_boundary))
        return rejected(1);
    auto* image = reinterpret_cast<void*>(image_base_);
    const bool rearm_completed_origin = tick_advance_.phase == TickAdvancePhase::Releasing
        && pause_boundary_ == PauseBoundary::CompletedApplication
        && application_phase_ == ApplicationPhase::Idle
        && simulation_->continuation().tick == tick_advance_.origin
        && EngineField<std::uint64_t>(image, 0x4197170) == interior_epoch_;
    // A completed-origin tick request transfers only its display resource.
    // The previous application and task must still be fully completed.
    if (interior_phase_ == InteriorPhase::Resumed
        && (surface_event_ || (!rearm_completed_origin && Horse::GameImGui::PresentHook::instance().replay_surface_ready())
            || (!rearm_completed_origin && EngineField<std::uint64_t>(image, 0x4197170) <= interior_epoch_)
            || !executor_.idle() || !executor_.arena_empty() || !simulation_->interval_complete()
            || executor_.task_groups().pending_manager_task())) return rejected(2);
    // First experiment: the verified retail fixed-rate mode, with wall-based
    // pacing. Benchmark/fixed-time alternatives need their own clock audit.
    if (EngineField<bool>(image, 0x416a943) || EngineField<bool>(image, 0x416a944)
        || !(EngineField<std::uint8_t>(engine_, 0x648) & 0x40)
        || EngineField<float>(engine_, 0x64c) != 60.0f) return rejected(3);
    const auto framework = GetModuleHandleW(L"UE4SS.dll");
    defer_engine_post_ = reinterpret_cast<EnginePost>(GetProcAddress(framework, "UE4SS_DeferEngineTickPost"));
    complete_engine_post_ = reinterpret_cast<EnginePost>(GetProcAddress(framework, "UE4SS_CompleteEngineTickPost"));
    if (!defer_engine_post_ || !complete_engine_post_) return rejected(4);
    interior_target_ = tick;
    if(SeekOwnsExecution() && HistoricalExecutionAdmitted() && seek_.advance_failure_tick
        && *seek_.advance_failure_tick>simulation_->continuation().tick && *seek_.advance_failure_tick<tick)
        interior_target_=*seek_.advance_failure_tick;
    interior_context_ = context;
    interior_observer_ = observer;
    interior_updates_ = 0;
    interior_resume_requested_ = false;
    pause_boundary_ = boundary;
    interior_previous_visible_ = Horse::GameImGui::visible();
    if (!QueueSurface(SurfaceCommand::Arm)) return rejected(5);
    Horse::GameImGui::set_visible(true);
    interior_phase_ = InteriorPhase::Arming;
    return true;
}

Status Sc6ReplayHost::Resume() noexcept
{
    if(SeekOwnsExecution() && !seek_driving_) return Status::failure(FailureCode::IllegalTransition);
    if(active_!=this || GetCurrentThreadId()!=thread_) return Status::failure(FailureCode::WrongThread);
    if(!index_.AllowsPlaybackFrom(simulation_->continuation().tick))
        return Status::failure(FailureCode::InvalidConfiguration);
    if(tick_advance_.phase==TickAdvancePhase::Settling) return Status::failure(FailureCode::IllegalTransition);
    if(capture_operation_.phase!=CapturePhase::Idle) return Status::failure(FailureCode::IllegalTransition);
    if (particle_command_pending_.load() || (particle_copy_ && particle_copy_->blocks_resume()))
        return Status::failure(FailureCode::IllegalTransition);
    if (historical_restore_ || checkpoint_restoring_ || active_ != this || GetCurrentThreadId() != thread_ || depth_ || !CheckBinding()
        || interior_phase_ != InteriorPhase::Holding)
        return Status::failure(FailureCode::IllegalTransition);
    interior_resume_requested_ = true;
    return Status::success();
}

Sc6ReplayHost::InteriorWitness Sc6ReplayHost::ReadInteriorWitness() const noexcept
{
    InteriorWitness witness{};
    witness.phase = interior_phase_;
    if (active_ != this || GetCurrentThreadId() != thread_) return witness;
    witness.tick = simulation_ ? simulation_->continuation().tick : 0;
    witness.epoch = EngineField<std::uint64_t>(reinterpret_cast<void*>(image_base_), 0x4197170);
    witness.application_updates = interior_updates_;
    auto& surface = Horse::GameImGui::PresentHook::instance();
    witness.surface_frames = surface.replay_surface_frames();
    witness.surface_bytes = surface.replay_surface_bytes();
    witness.ui_resume_requests = surface.replay_resume_requests();
    witness.ui_step_requests = surface.replay_step_requests();
    witness.ui_cancel_requests = surface.replay_cancel_requests();
    witness.pending_task = executor_.task_groups().pending_manager_task();
    witness.world = world_;
    witness.surface_pending = surface_event_ != nullptr;
    witness.boundary = pause_boundary_;
    witness.completed_applications = completed_applications_;
    witness.application_idle = application_phase_ == ApplicationPhase::Idle;
    witness.engine_idle = engine_idle();
    witness.world_idle = executor_.idle();
    witness.arena_empty = executor_.arena_empty();
    return witness;
}

bool Sc6ReplayHost::HasInteriorContinuation() const noexcept
{
    return (!engine_idle() || pause_boundary_ == PauseBoundary::CompletedApplication)
        && (interior_phase_ == InteriorPhase::Holding
        || interior_phase_ == InteriorPhase::Releasing || interior_phase_ == InteriorPhase::Failed);
}

bool Sc6ReplayHost::TrySuspendInterior()
{
    if(tick_advance_.phase==TickAdvancePhase::Settling) return false;
    if (interior_phase_ != InteriorPhase::Armed || pause_boundary_ != PauseBoundary::SimulationTick) return false;
    const auto tick = simulation_->continuation().tick;
    if (tick < interior_target_) return false;
    auto& surface = Horse::GameImGui::PresentHook::instance();
    if (tick != interior_target_ || engine_phase_ != EnginePhase::World
        || !executor_.task_groups().pending_manager_task() || surface_event_
        || !surface.replay_surface_ready())
    {
        RC::Output::send<RC::LogLevel::Warning>(STR(
            "[HorseMod] interior admission failed target={} tick={} engine_phase={} simulation_phase={} "
            "pending_task={} surface_pending={} surface_ready={} surface_bytes={}\n"),
            interior_target_, tick, static_cast<unsigned>(engine_phase_),
            static_cast<unsigned>(simulation_->continuation().phase),
            executor_.task_groups().pending_manager_task() != nullptr,
            surface_event_ != nullptr, surface.replay_surface_ready(), surface.replay_surface_bytes());
        interior_phase_ = InteriorPhase::Failed;
        if (tick_advance_.phase == TickAdvancePhase::Advancing) {
            tick_advance_.phase = TickAdvancePhase::Failed;
            tick_advance_.failure = FailureCode::AdvanceFailed;
        }
        return false; // No suspension began; finish the current native work.
    }
    const auto held = BeginHold(true);
    if (tick_advance_.phase == TickAdvancePhase::Advancing)
    {
        tick_advance_.phase = held ? TickAdvancePhase::Held : TickAdvancePhase::Failed;
        if (!held) tick_advance_.failure = FailureCode::AdvanceFailed;
    }
    return held;
}

void Sc6ReplayHost::TrySuspendApplication()
{
    // A vetoed lifetime operation invalidates C, not B. Finish this native
    // application and hold its actual disposal boundary before another tick.
    // This boundary is never reported as successful exact-target completion.
    if((HistoricalParticleAbortPending() || HistoricalConsumerAbortPending()) && interior_phase_!=InteriorPhase::Holding
        && application_phase_==ApplicationPhase::Idle && engine_idle()) {
        tick_advance_.phase=TickAdvancePhase::Settling;
        tick_advance_context_=this;tick_advance_observer_=&ObserveSeekHold;
    }
    // A playback pause has no exact tick target. Finish the admitted native
    // application, including all repeats, then select its actual boundary.
    // Exact seek and single-step requests never enter this mode.
    if(pause_boundary_==PauseBoundary::NextCompletedApplication && interior_phase_==InteriorPhase::Armed
        && application_phase_==ApplicationPhase::Idle && engine_idle()
        && simulation_->continuation().tick>=interior_target_) {
        const auto minimum=interior_target_;
        interior_target_=simulation_->continuation().tick;
        pause_boundary_=PauseBoundary::CompletedApplication;
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] playback pause boundary selected minimum={} actual={} application_complete=true\n"),minimum,interior_target_);
    }
    if(tick_advance_.phase==TickAdvancePhase::Settling && engine_idle() && !engine_post_deferred_) {
        pause_boundary_=PauseBoundary::CompletedApplication;
        interior_phase_=InteriorPhase::Armed;
        interior_target_=simulation_->continuation().tick;
        interior_context_=tick_advance_context_;interior_observer_=tick_advance_observer_;
        interior_updates_=0;
        interior_resume_requested_=historical_restore_ && historical_restore_->execution
            && historical_restore_->execution->ownership.phase()==ReplaySeekOwnership::Phase::CompletingResumedTails;
    }
    if (pause_boundary_ != PauseBoundary::CompletedApplication || interior_phase_ != InteriorPhase::Armed
        || simulation_->continuation().tick < interior_target_) return;
    // Host checkpoint and end requests can be armed after the preceding Present.
    // At its exact completed application, wait for the queued native Present
    // to publish the display instead of admitting another gameplay interval.
    if(IndexBoundaryRequiresCompletion() && simulation_->continuation().tick==interior_target_
        && simulation_->interval_complete() && application_phase_==ApplicationPhase::Idle
        && engine_idle() && executor_.idle() && executor_.arena_empty()
        && !executor_.task_groups().pending_manager_task() && !surface_event_
        && !Horse::GameImGui::PresentHook::instance().replay_surface_ready()) return;
    // This is an exact completed-application boundary, not seek snapping.
    // No expired native graph event is retained or restored here.
    if (simulation_->continuation().tick != interior_target_ || !simulation_->interval_complete()
        || application_phase_ != ApplicationPhase::Idle || !engine_idle() || !executor_.idle()
        || !executor_.arena_empty() || executor_.task_groups().pending_manager_task() || surface_event_
        || !Horse::GameImGui::PresentHook::instance().replay_surface_ready())
    {
        interior_phase_ = InteriorPhase::Failed;
        if(tick_advance_.phase==TickAdvancePhase::Settling) {
            tick_advance_.phase=TickAdvancePhase::Failed;tick_advance_.failure=FailureCode::AdvanceFailed;
        }
        return;
    }
    const bool held=BeginHold(false);
    if(tick_advance_.phase==TickAdvancePhase::Settling) {
        tick_advance_.phase=held?TickAdvancePhase::Settled:TickAdvancePhase::Failed;
        tick_advance_.target=simulation_->continuation().tick;
        if(!held) tick_advance_.failure=FailureCode::AdvanceFailed;
    }
}

bool Sc6ReplayHost::BeginHold(bool defer_post)
{
    auto* image = reinterpret_cast<void*>(image_base_);
    interior_epoch_ = EngineField<std::uint64_t>(image, 0x4197170);
    interior_wall_reference_ = EngineField<double>(image, 0x43b3778);
    LARGE_INTEGER counter{};
    if (!QueryPerformanceCounter(&counter)) __fastfail(FAST_FAIL_INVALID_ARG);
    if (defer_post && !engine_post_deferred_) {
        if (!defer_engine_post_(this)) __fastfail(FAST_FAIL_INVALID_ARG);
        engine_post_deferred_ = true;
    }
    interior_qpc_ = counter.QuadPart;
    interior_ui_resume_baseline_=Horse::GameImGui::PresentHook::instance().replay_resume_requests();
    interior_ui_step_baseline_=Horse::GameImGui::PresentHook::instance().replay_step_requests();
    interior_ui_cancel_baseline_=Horse::GameImGui::PresentHook::instance().replay_cancel_requests();
    next_surface_ = std::chrono::steady_clock::now();
    interior_phase_ = InteriorPhase::Holding;
    if (pause_boundary_ == PauseBoundary::CompletedApplication)
    {
        // Enqueue after the completed application's native render commands.
        // Release on resume detaches the live surface; retained checkpoints
        // then keep their own image instead of receiving subsequent writes.
        try { held_surface_ = std::make_shared<SurfaceSnapshot>(); }
        catch (...) { interior_phase_ = InteriorPhase::Failed; return false; }
        held_surface_->session = checkpoint_session_;
        held_surface_->tick = simulation_->continuation().tick;
        held_surface_->epoch = interior_epoch_;
        if (!QueueSurface(SurfaceCommand::RetainCheckpoint))
        { held_surface_.reset(); interior_phase_ = InteriorPhase::Failed; return false; }
    }
    return true;
}

void Sc6ReplayHost::AdvanceInteriorHold()
{
    struct Driving {bool& flag;bool prior;Driving(bool& f,bool value):flag(f),prior(f){flag=value;}~Driving(){flag=prior;}}
        driving(seek_driving_,SeekOwnsExecution());
    ++interior_updates_;
    auto* image = reinterpret_cast<void*>(image_base_);
    if (!CheckBinding() || EngineField<std::uint64_t>(image, 0x4197170) != interior_epoch_
        || EngineField<double>(image, 0x43b3778) != interior_wall_reference_)
        interior_phase_ = InteriorPhase::Failed;
    bool surface_complete = false;
    if (surface_event_ && !PollSurface(surface_complete))
    {
        if (historical_restore_ && (surface_command_ == SurfaceCommand::InstallCheckpoint
            || surface_command_ == SurfaceCommand::PublishCheckpoint))
        {
            historical_restore_->witness.failure = FailureCode::PresentationFailed;
            historical_restore_->witness.phase = RestoreOperationPhase::Recovering;
        }
        else interior_phase_ = InteriorPhase::Failed;
    }
    if ((tick_advance_.phase == TickAdvancePhase::Stepping || tick_advance_.phase == TickAdvancePhase::Settling)
        && interior_phase_ != InteriorPhase::Failed) {
        if (surface_event_ || particle_command_pending_.load()) return;
        if (!engine_post_deferred_ || engine_phase_ != EnginePhase::World
            || !executor_.task_groups().pending_manager_task()
            || !Horse::GameImGui::PresentHook::instance().replay_surface_ready()) {
            tick_advance_.phase=TickAdvancePhase::Failed;tick_advance_.failure=FailureCode::IllegalTransition;
            interior_phase_=InteriorPhase::Failed;return;
        }
        LARGE_INTEGER counter{};
        if (!QueryPerformanceCounter(&counter)) __fastfail(FAST_FAIL_INVALID_ARG);
        // Account for this hold once without changing the pending world's
        // delta, epoch, task event or allocation mark.
        EngineField<double>(image,0x43b3778)=interior_wall_reference_
            + static_cast<double>(counter.QuadPart-interior_qpc_)*EngineField<double>(image,0x415cd90);
        const bool settling=tick_advance_.phase==TickAdvancePhase::Settling;
        interior_target_=tick_advance_.target;interior_context_=tick_advance_context_;
        interior_observer_=tick_advance_observer_;interior_updates_=0;interior_resume_requested_=false;
        // Completing pending work is not a playback Resume. Engine-post
        // observers must see the owned transition until the completed hold.
        interior_phase_=InteriorPhase::Armed;
        if(!settling) tick_advance_.phase=TickAdvancePhase::Advancing;
        // Managed target completion is exact. Recovery may discard further C
        // traversals before reinstalling B, and explicit Resume authorizes
        // playback. Unowned pre-capture completion retains its documented
        // preparation semantics: B is captured at the resulting actual tick.
        const auto owner=historical_restore_ && historical_restore_->execution
            ? historical_restore_->execution->ownership.phase():ReplaySeekOwnership::Phase::BRetained;
        const bool exact_tail=settling && owner==ReplaySeekOwnership::Phase::CompletingTargetTails;
        simulation_->SetTailOnly(exact_tail);
        ++depth_;
        while (!engine_idle()) {
            AdvanceEngine();
            if(simulation_->tail_blocked()) {
                tick_advance_.phase=TickAdvancePhase::CompletionBlocked;
                tick_advance_.target=simulation_->continuation().tick;
                interior_target_=tick_advance_.target;
                if(!BeginHold(true)) {tick_advance_.phase=TickAdvancePhase::Failed;tick_advance_.failure=FailureCode::PresentationFailed;}
                break;
            }
            if(TrySuspendInterior()) break;
        }
        --depth_;
        simulation_->SetTailOnly(false);
        if (engine_idle()) {
            if (!complete_engine_post_(this)) __fastfail(FAST_FAIL_INVALID_ARG);
            engine_post_deferred_=false;
        }
        return;
    }
    AdvanceCaptureOperation();
    AdvanceHistoricalRestore();
    AdvanceSeek();
    auto& timeline_ui=Horse::GameImGui::PresentHook::instance();
    const auto intent=timeline_ui.take_replay_seek_request();
    if(timeline_ui.replay_cancel_requests()!=interior_ui_cancel_baseline_
        || timeline_ui.replay_resume_requests()!=interior_ui_resume_baseline_) queued_indexed_seek_.reset();
    else if(intent!=timeline_ui.no_seek_request) {
        queued_indexed_seek_.reset();
        if(intent==timeline_ui.exit_replay_request) {
            if(!RequestMenuExit()) indexed_seek_failure_=FailureCode::IllegalTransition;
            // The native stop interceptor now owns teardown. Never dispatch
            // another seek/resume using the pre-exit witness in this update.
            seek_driving_=false;return;
        }
        const auto status=intent==timeline_ui.previous_round_request?RequestIndexedRound(-1)
            :intent==timeline_ui.next_round_request?RequestIndexedRound(1):RequestIndexedSeek(intent);
        if(!status.ok()) indexed_seek_failure_=status.code;
    }
    DriveIndexedSeekIntent();
    const auto witness = ReadInteriorWitness();
    // A settled seek retains B while its handed-off C owners can execute one
    // more tick. The click must still pass StepHistoricalExecution before
    // changing ownership; displaying the control never admits execution.
    const bool retained_step=historical_restore_ && HistoricalExecutionAdmitted(true)
        && historical_restore_->execution->ownership.phase()==ReplaySeekOwnership::Phase::CSettled
        && historical_restore_->witness.phase==RestoreOperationPhase::Held
        && !historical_restore_->witness.pending && !historical_restore_->cancel
        && seek_.witness.phase==SeekPhase::Held && !seek_.witness.pending && !seek_.cancel;
    const bool can_step=interior_phase_==InteriorPhase::Holding
        && (retained_step || (!historical_restore_ && !checkpoint_restoring_))
        && capture_operation_.phase==CapturePhase::Idle && !particle_command_pending_.load()
        && (retained_step || !particle_copy_ || !particle_copy_->blocks_resume())
        && (!SeekOwnsExecution() || seek_.witness.phase==SeekPhase::Held || seek_.witness.phase==SeekPhase::Cancelled)
        && (!surface_event_ || (pause_boundary_==PauseBoundary::SimulationTick && surface_command_==SurfaceCommand::Draw))
        && witness.tick!=UINT64_MAX
        && (index_.witness().phase!=ReplayTickIndex::Phase::Complete || witness.tick+1<index_.witness().entries);
    const bool can_cancel=SeekOwnsExecution() && !seek_.cancel && !seek_.witness.commit_decided
        && (seek_.witness.phase!=SeekPhase::Held || historical_restore_) && seek_.witness.phase!=SeekPhase::Cancelled
        && (seek_.witness.phase!=SeekPhase::Failed || historical_restore_);
    Horse::GameImGui::PresentHook::instance().publish_replay_controls(witness.tick,can_step,can_cancel,
        seek_.witness.phase==SeekPhase::Failed);
    const auto indexed=index_.witness();
    std::uint64_t first_checkpoint=UINT64_MAX;
    for(const auto& weak:retained_checkpoints_) {
        const auto image=weak.lock();
        if(image && image->session==checkpoint_session_ && image->historical_target_shape_supported())
            first_checkpoint=(std::min)(first_checkpoint,image->execution.tick);
    }
    ReplayTickIndex::Entry indexed_tick{};
    const bool indexed_coordinate=indexed.phase==ReplayTickIndex::Phase::Complete
        && ReadIndexEntry(witness.tick,&indexed_tick);
    Horse::GameImGui::PresentHook::instance().publish_replay_timeline(
        indexed.phase==ReplayTickIndex::Phase::Complete,indexed.entries?indexed.entries-1:0,
        static_cast<unsigned>(indexed_seek_failure_),first_checkpoint,
        indexed_coordinate?indexed_tick.round:-1,indexed_tick.round_tick,indexed.unsupported_native_tail,CanRequestMenuExit());
    // A diagnostic observer may retain the hold until its independent checks
    // finish. Without one, UI/API requests are consumed here, never in render
    // callbacks or while an engine/world step is on the stack.
    seek_driving_=false; // External observers do not inherit mutation ownership.
    if(pause_monitor_) {
        const auto before_monitor=interior_phase_;
        try {pause_monitor_(pause_monitor_context_,witness);}
        catch(...) {interior_phase_=InteriorPhase::Failed;}
        // A monitor may request completion or unsubscribe after observing a
        // target. Never dispatch UI work using the pre-notification phase.
        if(!ContinueAfterPauseMonitor(before_monitor,interior_phase_,tick_advance_.phase)) return;
    }
    if(!interior_observer_ && can_step && witness.ui_step_requests!=interior_ui_step_baseline_) {
        // The render thread only publishes intent. Coalesce repeated clicks
        // into one step at this safe boundary; the new hold resets admission.
        interior_ui_step_baseline_=witness.ui_step_requests;
        const auto step=AdvanceToTick(witness.tick+1,nullptr,nullptr);
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] replay UI step dispatched origin={} target={} phase={} observer=false\n"),
            witness.tick,witness.tick+1,static_cast<unsigned>(step.phase));
        if(step.phase==TickAdvancePhase::Stepping || step.phase==TickAdvancePhase::Releasing) return;
    }
    const bool requested_resume = interior_observer_ ? interior_observer_(interior_context_, witness)
        : (interior_resume_requested_ || witness.ui_resume_requests != interior_ui_resume_baseline_);
    const bool resume=requested_resume && index_.AllowsPlaybackFrom(witness.tick);
    seek_driving_=SeekOwnsExecution();
    // Admission may owe asynchronous resource retirement after an observer's
    // one-shot release request. Preserve that request through those updates;
    // consuming it is the transition to Releasing, not the observer callback.
    if (capture_operation_.phase!=CapturePhase::Idle || tick_advance_.phase == TickAdvancePhase::Stepping) return;
    interior_resume_requested_ = interior_resume_requested_ || resume;
    if (Sc6ReplayParticleCopy::capture_retirement_pending())
    {
        if (particle_copy_action_ == ParticleCopyAction::RetireCaptures && particle_command_failed_.load())
        { interior_phase_ = InteriorPhase::Failed; return; }
        if (!surface_event_ && !particle_command_pending_.load())
        {
            Sc6ReplayParticleCopy::Witness retirement{};
            bool pending{};
            if (!ParticleCopyExperiment(ParticleCopyAction::RetireCaptures, &retirement, &pending))
                interior_phase_ = InteriorPhase::Failed;
        }
        return; // Last-reference release is not render-thread retirement.
    }
    if (interior_phase_ == InteriorPhase::Failed) {
        if (tick_advance_.phase != TickAdvancePhase::Idle) {
            tick_advance_.phase = TickAdvancePhase::Failed;
            tick_advance_.failure = FailureCode::AdvanceFailed;
        }
        if (!surface_event_ && !particle_command_pending_.load()
            && Horse::GameImGui::PresentHook::instance().replay_retirement_pending()) QueueSurface(SurfaceCommand::RetireHeldDisplay);
        return;
    }
    const bool owned_advance=HistoricalExecutionAdmitted() && tick_advance_.phase==TickAdvancePhase::Releasing;
    if (interior_phase_ == InteriorPhase::Holding && interior_resume_requested_
        && (index_.AllowsPlaybackFrom(simulation_->continuation().tick)
            || tick_advance_.phase==TickAdvancePhase::Releasing || tick_advance_.phase==TickAdvancePhase::Settling)
        && ((!historical_restore_ && !checkpoint_restoring_) || owned_advance) && !surface_event_
        && !particle_command_pending_.load() && (!particle_copy_ || !particle_copy_->blocks_resume() || owned_advance))
    {
        // An adjacent target may precede the next native Present. Transfer
        // the last coherent completed display through rearming; it is display
        // only, never proof that target render tails have already executed.
        const auto release = tick_advance_.phase==TickAdvancePhase::Releasing
            ? SurfaceCommand::ReleaseForAdvance : SurfaceCommand::Release;
        if (!QueueSurface(release)) { interior_phase_ = InteriorPhase::Failed; return; }
        if (tick_advance_.phase == TickAdvancePhase::Held || tick_advance_.phase==TickAdvancePhase::Settled)
            tick_advance_.phase = TickAdvancePhase::Idle;
        interior_phase_ = InteriorPhase::Releasing;
        return;
    }
    if (interior_phase_ == InteriorPhase::Releasing && surface_complete)
    {
        // The native pacing function is the only writer of this wall reference.
        // Account for the deliberately held wall duration once. Keep the
        // unfinished frame's delta and engine epoch unchanged; do not rewind
        // the global engine counter. Absolute-time presentation consumers are
        // outside the correctness claim of this first fixed-rate experiment.
        if (EngineField<bool>(image, 0x416a943) || EngineField<bool>(image, 0x416a944)
            || !(EngineField<std::uint8_t>(engine_, 0x648) & 0x40)
            || EngineField<float>(engine_, 0x64c) != 60.0f)
        { interior_phase_ = InteriorPhase::Failed; return; }
        LARGE_INTEGER counter{};
        if (!QueryPerformanceCounter(&counter)) __fastfail(FAST_FAIL_INVALID_ARG);
        const double held_seconds = static_cast<double>(counter.QuadPart - interior_qpc_)
            * EngineField<double>(image, 0x415cd90);
        EngineField<double>(image, 0x43b3778) = interior_wall_reference_ + held_seconds;
        Horse::GameImGui::set_visible(interior_previous_visible_ || index_.witness().phase==ReplayTickIndex::Phase::Complete);
        interior_phase_ = InteriorPhase::Resumed;
        // Completed applications have already delivered EngineTickPost and
        // their outer tail. Resume enters a fresh application on the next
        // GuardedMain call; neither post callbacks nor tails run twice.
        if (pause_boundary_ == PauseBoundary::CompletedApplication) return;
        ++depth_;
        while (!engine_idle()) AdvanceEngine();
        --depth_;
        if (!complete_engine_post_(this)) __fastfail(FAST_FAIL_INVALID_ARG);
        engine_post_deferred_=false;
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (interior_phase_ == InteriorPhase::Holding && !surface_event_ && !particle_command_pending_.load() && now >= next_surface_)
    {
        next_surface_ = now + std::chrono::microseconds(16667);
        if (!QueueSurface(SurfaceCommand::Draw)) interior_phase_ = InteriorPhase::Failed;
    }
    // No native Windows/Slate pump here: either can admit gameplay work or
    // nested task waits. The first bounded hold uses the existing render-side
    // controller polling and overlay drawing. General window-event admission
    // (including resize/exit) remains part of the required lifecycle work.
}
