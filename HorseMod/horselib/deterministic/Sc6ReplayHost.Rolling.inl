// Depth-seven execution composes the existing capture/seek/retirement owners.
// Ring eviction drops only our reference; operation and GPU pins remain charged.
bool Sc6ReplayHost::ProbeGroundMotion(std::uint32_t protocol,std::uint64_t expected_tick,
    Sc6ReplayGroundDebrisState::MotionProbeWitness* witness) noexcept
{
    auto* self=active_;
    if(witness)*witness={};
    if(!witness || protocol!=1 || !self || self->failed_ || self->motion_probe_used_
        || GetCurrentThreadId()!=self->thread_ || !self->simulation_
        || self->simulation_->continuation().tick!=expected_tick
        || self->interior_phase_!=InteriorPhase::Holding || self->pause_boundary_!=PauseBoundary::CompletedApplication
        || !self->engine_idle() || !self->world_idle() || !self->arena_empty() || self->surface_event_
        || self->historical_restore_ || self->checkpoint_restoring_ || self->seek_retirement_pending_
        || self->rolling_.witness.phase!=RollingPhase::Complete)return false;
    self->motion_probe_used_=true;
    UcrtRandBrokerImage before{},after{};
    if(!self->checkpoint_broker_ || !self->checkpoint_broker_->Capture(self->thread_,before).ok())return false;
    const auto budget=self->AdmissionRemaining();
    auto status=Sc6ReplayGroundDebrisState::ProbeMotion(self->image_base_,self->manager_,self->world_,budget,*witness);
    const bool rng=self->checkpoint_broker_->Capture(self->thread_,after).ok() && before==after;
    if(!rng)witness->check="probe_shared_rng_changed";
    if(!status.ok() || !rng) {
        // This diagnostic has no pending A/B transaction. Keep the completed
        // native graph owned and held; the test owner terminates on failure.
        self->failed_=true;self->interior_phase_=InteriorPhase::Failed;
        self->tick_advance_.phase=TickAdvancePhase::Failed;
        self->tick_advance_.failure=status.ok()?FailureCode::RestoreVerificationFailed:status.code;
        return false;
    }
    return true;
}

void Sc6ReplayHost::EmitRollingTelemetry() noexcept
{
    auto& telemetry=rolling_.telemetry;
    while(telemetry.emitted<telemetry.size) {
        const auto& row=telemetry.rows[telemetry.emitted];
        if(row.status==ReplayRollingTelemetry::Status::Active)break;
        ++telemetry.emitted;
        const auto status=row.status==ReplayRollingTelemetry::Status::Complete?"complete":
            row.status==ReplayRollingTelemetry::Status::Aborted?"aborted":"terminal_no_next_checkpoint";
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] rolling timing protocol=1 ordinal={} tick={} status={} full_update_us={} backlog={} preparation_us={} publication_us={} resimulation_us={} application_us={} forward_us={} capture_us={} retirement_us={}\n"),
            row.ordinal,row.tick,RC::to_generic_string(status),row.elapsed_us,row.backlog,
            row.phases[0],row.phases[1],row.phases[2],row.phases[3],row.phases[4],row.phases[5],row.phases[6]);
    }
}

bool Sc6ReplayHost::FailRolling(FailureCode code,const char* check) noexcept
{
    auto& w=rolling_.witness;
    if(w.phase!=RollingPhase::Failed) {
        rolling_.telemetry.Abort(ReplayRollingTelemetry::Now());EmitRollingTelemetry();
        w.failure=code;w.phase=RollingPhase::Failed;
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] rolling failed check={} code={} current={} target={} cycles={} retained_window=true\n"),
            RC::to_generic_string(check),static_cast<unsigned>(code),simulation_->continuation().tick,w.target,w.cycles);
    }
    return false; // Keep the native transaction and B; never resume partial C.
}

bool Sc6ReplayHost::StoreRollingCheckpoint(CheckpointHandle image) noexcept
{
    auto& r=rolling_;auto& w=r.witness;
    if(!image || !image->valid || image->session!=checkpoint_session_ || image->execution.tick!=w.current
        || !ReplayStatePolicy::Accepts(image->state_policy)
        || image->boundary!=PauseBoundary::CompletedApplication || image->task || image->event
        || !image->source.SameRecording(r.source) || image->input_revision!=r.revision
        || !image->historical_target_shape_supported())return FailRolling(FailureCode::GenerationMismatch,"checkpoint_domain");
    auto& slot=r.checkpoints[w.current%r.checkpoints.size()];
    if(slot && (w.current<8 || slot->execution.tick!=w.current-8))
        return FailRolling(FailureCode::IllegalTransition,"ring_turnover");
    slot=std::move(image);
    r.generations[w.current%r.generations.size()]=w.checkpoint_generation;
    return true;
}

bool Sc6ReplayHost::PrepareRollingCheckpointSlot() noexcept
{
    auto& r=rolling_;auto& w=r.witness;
    if(seek_retirement_failed_)return FailRolling(FailureCode::IllegalTransition,"ring_retirement_failed");
    if(seek_retirement_pending_)return false;
    if(historical_restore_ || checkpoint_restoring_ || released_seek_checkpoint_)
        return FailRolling(FailureCode::IllegalTransition,"ring_retirement_owner");
    if(!CanRetireSeekCheckpoint())return false;
    auto& slot=r.checkpoints[w.current%r.checkpoints.size()];
    if(!slot)return true;
    if(w.current<8 || slot->execution.tick!=w.current-8)
        return FailRolling(FailureCode::IllegalTransition,"ring_turnover");
    // Called only after the next ordinary forward update has completed. The
    // previous correction and its complete B have already committed/released.
    // T-8 is no longer in the next seven-tick window. Hand off only this ring
    // pin; external/GPU pins remain charged and native retirement is joined by
    // AdvanceSeekRetirement before any replacement capture can allocate.
    released_seek_checkpoint_=std::move(slot);
    seek_retirement_pending_=true;
    return false;
}

bool Sc6ReplayHost::HasRollingReplacementCapacity(std::size_t minimum_private_capture_bytes) const noexcept
{
    // Necessary capacity floor before A publication, not a promise that later
    // mutable/native captures fit. Every producer still checks the live shared
    // remainder, including B, provisional owners, scratch and retirement.
    return minimum_private_capture_bytes
        && minimum_private_capture_bytes<=AdmissionRemaining()/rolling_.replacement.size();
}

bool Sc6ReplayHost::ValidateRollingReplacement() const noexcept
{
    const auto& r=rolling_;const auto& w=r.witness;
    if(!r.correcting || !r.correction_installed || !r.rebuild_complete || !r.proposed_revision
        || w.current<7 || w.checkpoint_generation==UINT64_MAX)return false;
    for(std::uint64_t offset=0;offset<8;++offset) {
        const auto tick=w.current-offset;const auto slot=tick%r.replacement.size();
        const auto& image=r.replacement[slot];
        if(!image || !image->valid || image->session!=checkpoint_session_ || image->execution.tick!=tick
            || !ReplayStatePolicy::Accepts(image->state_policy)
            || image->input_revision!=r.proposed_revision || !image->source.SameRecording(r.source)
            || image->boundary!=PauseBoundary::CompletedApplication || image->task || image->event
            || !image->historical_target_shape_supported()
            || r.replacement_generations[slot]!=w.checkpoint_generation+1)return false;
    }
    return true;
}

bool Sc6ReplayHost::CommitRollingReplacement() noexcept
{
    auto& r=rolling_;auto& w=r.witness;
    if(historical_restore_ || checkpoint_restoring_ || seek_retirement_pending_
        || seek_.witness.release_result!=SeekReleaseResult::Completed || !ValidateRollingReplacement())return false;
    // Every check precedes this allocation-free publication. The old window
    // stays pinned until complete C/application/render commit has succeeded.
    r.checkpoints.swap(r.replacement);r.generations.swap(r.replacement_generations);
    r.revision=std::move(r.proposed_revision);r.input_history=r.replacement_history;
    w.source_revision=r.proposed_revision_id;++w.checkpoint_generation;++w.corrections_committed;
    ++r.next_correction;
    r.replacement={};r.replacement_generations={};r.correcting=r.correction_installed=r.rebuild_complete=false;
    // Destructors queue native/GPU retirement; the existing application owner
    // joins it before permitting another engine epoch.
    seek_retirement_pending_=true;
    return true;
}

bool Sc6ReplayHost::AdvanceRollingForward() noexcept
{
    auto& r=rolling_;auto& w=r.witness;
    if(w.current==UINT64_MAX)return FailRolling(FailureCode::CapacityExceeded,"tick_overflow");
    r.telemetry.Mark(ReplayRollingTelemetry::Phase::Forward,ReplayRollingTelemetry::Now());
    r.forward_started=std::chrono::steady_clock::now();w.target=w.current+1;w.phase=RollingPhase::Advancing;
    const auto next=AdvanceToTick(w.target,this,[](void* context,const InteriorWitness& held) {
        return static_cast<Sc6ReplayHost*>(context)->ObserveRollingHold(held);
    });
    return next.phase==TickAdvancePhase::Releasing || FailRolling(next.failure,"forward_request");
}

bool Sc6ReplayHost::RollingOperation(RollingAction action,const Checkpoint* first,std::uint64_t cycles,RollingWitness* output) noexcept
{
    auto* self=active_;
    if(!self || !output || GetCurrentThreadId()!=self->thread_ || self->depth_)return false;
    auto& r=self->rolling_;auto& w=r.witness;*output=w;
    if(action==RollingAction::Read)return true;
    if(action==RollingAction::Release) {
        if(w.phase!=RollingPhase::Complete || self->historical_restore_ || self->checkpoint_restoring_
            || self->seek_retirement_pending_ || Sc6ReplayParticleCopy::capture_retirement_pending())return false;
        r={};self->interior_context_=nullptr;self->interior_observer_=nullptr;return true;
    }
    if(action!=RollingAction::Begin || w.phase!=RollingPhase::Idle || !cycles || cycles>600
        || self->SeekOwnsExecution() || self->historical_restore_ || self->checkpoint_restoring_
        || self->capture_operation_.phase!=CapturePhase::Idle || !self->CanRetireSeekCheckpoint()
        || self->interior_phase_!=InteriorPhase::Holding || self->pause_boundary_!=PauseBoundary::CompletedApplication
        || self->surface_event_ || self->particle_command_pending_.load())return false;
    auto image=self->ResolveOwnedCheckpoint(first);
    if(!image || image->execution.tick!=self->simulation_->continuation().tick || image->execution.tick>UINT64_MAX-cycles-8
        || !image->source.tracker_active || image->source.round<0)return false;
    w.first=w.current=image->execution.tick;w.requested=cycles;r.source=image->source;r.revision=image->input_revision;
    r.telemetry.warmup_started=ReplayRollingTelemetry::Now();
    w.source_revision=r.revision?r.revision->id:0;
    if(!self->StoreRollingCheckpoint(std::move(image))) {*output=w;return false;}
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] rolling started first={} depth=7 requested={} window=8 session={} round={}\n"),
        w.first,w.requested,self->checkpoint_session_,r.source.round);
    const auto accepted=self->AdvanceRollingForward();*output=w;return accepted;
}

bool Sc6ReplayHost::BeginRollingSchedule(std::uint32_t protocol,const Checkpoint* first,std::uint64_t cycles,
    std::span<const ReplayCorrectionRequest> requests,RollingWitness* output) noexcept
{
    auto* self=active_;
    if(protocol!=rolling_schedule_protocol || !self || !output || GetCurrentThreadId()!=self->thread_
        || self->rolling_.witness.phase!=RollingPhase::Idle || !cycles || cycles>600)return false;
    auto image=self->ResolveOwnedCheckpoint(first);
    if(!image || image->source.round<0 || image->source.cursor<0 || image->execution.tick>UINT64_MAX-cycles-8)return false;
    ReplayCorrectionSchedule::Handle schedule;
    const auto prepared=ReplayCorrectionSchedule::Create(requests,self->AdmissionRemaining(),schedule);
    if(!prepared.ok()){*output={};output->failure=prepared.code;return false;}
    // A stale first revision must be rejected before the host takes ownership
    // of a rolling window. Subsequent rows are ordered by Create and checked
    // against the installed revision at their exact arrival tick.
    const auto initial_revision=image->input_revision?image->input_revision->id:0;
    if(schedule->row(0).expected_revision!=initial_revision) {
        *output={};output->failure=FailureCode::GenerationMismatch;return false;
    }
    for(std::size_t i=0;i<schedule->size();++i) {
        const auto& row=schedule->row(i);
        if(row.session!=image->session || row.epoch!=static_cast<std::uint64_t>(image->source.round)+1
            || row.arrival_tick<image->execution.tick+7 || row.arrival_tick>image->execution.tick+6+cycles
            || (i && schedule->row(i-1).arrival_tick==row.arrival_tick))return false;
    }
    if(!self->simulation_->SetBoundaryObserver(self,&ObserveIndexBoundary))return false;
    auto& r=self->rolling_;
    // The first forward request may synchronously observe an input boundary.
    // Publish both owned schedule bytes and its tick/sample history first.
    r.schedule=std::move(schedule);
    r.input_history.Begin(image->session,static_cast<std::uint64_t>(image->source.round)+1,
        image->source.round,image->execution.tick,static_cast<unsigned>(image->source.cursor));
    if(!RollingOperation(RollingAction::Begin,first,cycles,output)) {
        if(r.witness.phase==RollingPhase::Idle) {r.schedule.reset();r.input_history={};}
        return false;
    }
    for(std::size_t i=0;i<r.schedule->size();++i) {
        const auto& row=r.schedule->row(i);
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] rolling correction scheduled ordinal={} arrival={} expected_revision={}\n"),
            i+1,row.arrival_tick,row.expected_revision);
    }
    return true;
}

void Sc6ReplayHost::ObserveRollingInput(const Sc6ReplayExecutor::Continuation& execution) noexcept
{
    auto& r=rolling_;
    if(!r.schedule || r.witness.phase==RollingPhase::Failed || r.witness.phase==RollingPhase::Complete)return;
    if(r.witness.phase==RollingPhase::Seeking && !(r.correcting && r.correction_installed))return;
    ReplaySourceState source{};
    if(execution.phase!=Sc6ReplayExecutor::Phase::RepeatDecision || execution.cache_offset<0
        || !ReadCheckpointSource(source).ok() || !source.SameRecording(r.source)) {
        FailRolling(FailureCode::GenerationMismatch,"consumption_epoch");return;
    }
    __try {
        // 1403FCD10 consumes cache[(gameTime-cacheOffset)-1]. Validate
        // producer coordinates independently; ticks are never substituted.
        auto* log=EngineField<void*>(manager_,0x478);
        if(!log || EngineField<int>(log,0x3a0)!=source.round || EngineField<int>(log,0x3a4)!=source.cursor
            || source.cursor<=execution.cache_offset) {FailRolling(FailureCode::GenerationMismatch,"consumption_source");return;}
        auto& history=r.correcting?r.replacement_history:r.input_history;
        const auto status=history.Record(execution.tick,execution.publications,source.round,
            static_cast<unsigned>(source.cursor-execution.cache_offset-1));
        if(!status.ok())FailRolling(status.code,"consumption_order");
    } __except(EXCEPTION_EXECUTE_HANDLER) {FailRolling(FailureCode::ContextUnavailable,"consumption_binding");}
}

bool Sc6ReplayHost::DriveRollingReplacement() noexcept
{
    auto& r=rolling_;auto& w=r.witness;
    if(!r.correcting || r.rebuild_complete || seek_.cancel
        || (seek_.witness.phase!=SeekPhase::Restored && seek_.witness.phase!=SeekPhase::Advancing))return false;
    const auto fail=[&](FailureCode code,const char* check) {
        seek_.cancel=true;seek_.witness.failure=code;
        // Keep the rolling observer pending while the existing seek owner
        // retires provisional captures and recovers B. A terminal rolling
        // receipt here would make the observer stop before that recovery.
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] rolling replacement recovery requested check={} code={} arrival={} B_retained=true\n"),
            RC::to_generic_string(check),static_cast<unsigned>(code),w.current);
        return true;
    };
    if(surface_event_ || particle_command_pending_.load())return true;
    if(tick_advance_.phase==TickAdvancePhase::Failed)return fail(tick_advance_.failure,"replacement_advance");
    // AdvanceToTick/CompleteApplicationToHold are asynchronous. Their own
    // driver must finish the recorded traversal/tails before capture resumes.
    if(tick_advance_.phase==TickAdvancePhase::Releasing || tick_advance_.phase==TickAdvancePhase::Arming
        || tick_advance_.phase==TickAdvancePhase::Advancing || tick_advance_.phase==TickAdvancePhase::Stepping
        || tick_advance_.phase==TickAdvancePhase::Settling)return true;
    if(!HistoricalExecutionAdmitted() || interior_phase_!=InteriorPhase::Holding)return fail(FailureCode::IllegalTransition,"replacement_hold");
    const auto tick=simulation_->continuation().tick;
    if(tick<w.current-7 || tick>w.current)return fail(FailureCode::GenerationMismatch,"replacement_tick");
    if(!r.correction_installed) {
        if(seek_.witness.phase!=SeekPhase::Restored || tick!=w.current-7)return fail(FailureCode::IllegalTransition,"correction_install_boundary");
        const auto status=ReviseInputs(r.schedule->edits(r.next_correction),w.source_revision,r.proposed_revision_id);
        if(!status.ok())return fail(status.code,"correction_install");
        r.proposed_revision=input_source_->revision();r.correction_installed=true;
        const auto& a=r.checkpoints[tick%r.checkpoints.size()];
        r.replacement_history.Begin(checkpoint_session_,static_cast<std::uint64_t>(r.source.round)+1,r.source.round,tick,
            static_cast<unsigned>(a->source.cursor));
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] rolling correction installed arrival={} revision={} provisional_generation={} B_revision={}\n"),
            w.current,r.proposed_revision_id,w.checkpoint_generation+1,w.source_revision);
    }
    const auto& recorded=r.checkpoints[tick%r.checkpoints.size()];
    if(!recorded || recorded->execution.tick!=tick || recorded->boundary!=PauseBoundary::CompletedApplication)
        return fail(FailureCode::GenerationMismatch,"replacement_recorded_boundary");
    if(pause_boundary_==PauseBoundary::SimulationTick) {
        r.telemetry.Mark(ReplayRollingTelemetry::Phase::Application,ReplayRollingTelemetry::Now());
        const auto completed=CompleteApplicationToHold(this,&ObserveSeekHold);
        if(completed.phase!=TickAdvancePhase::Settling)return fail(completed.failure,"replacement_application_tail");
        return true;
    }
    if(capture_operation_.phase==CapturePhase::Idle) {
        CaptureWitness capture{};
        r.telemetry.Mark(ReplayRollingTelemetry::Phase::Capture,ReplayRollingTelemetry::Now());
        if(!BeginCorrectedCapture(&capture))return fail(FailureCode::CapturePreflightFailed,"replacement_capture_begin");
        return true;
    }
    if(capture_operation_.phase!=CapturePhase::Ready)return true;
    CaptureWitness capture{};CheckpointHandle image;
    if(!CaptureOperation(CaptureAction::Take,&capture,&image) || !image || image->execution.tick!=tick
        || image->input_revision!=r.proposed_revision)return fail(FailureCode::GenerationMismatch,"replacement_capture_take");
    r.replacement[tick%r.replacement.size()]=std::move(image);
    r.replacement_generations[tick%r.replacement_generations.size()]=w.checkpoint_generation+1;
    if(tick==w.current) {
        r.rebuild_complete=true;
        if(!ValidateRollingReplacement())return fail(FailureCode::GenerationMismatch,"replacement_validation");
        // Same-tick admission changes only the existing hold state, and runs
        // no callback, application update or native simulation traversal.
        if(AdvanceToTick(tick,this,&ObserveSeekHold).phase!=TickAdvancePhase::Held)
            return fail(FailureCode::IllegalTransition,"replacement_target_hold");
        return false;
    }
    r.telemetry.Mark(ReplayRollingTelemetry::Phase::Resimulation,ReplayRollingTelemetry::Now());
    const auto next=AdvanceToTick(tick+1,this,&ObserveSeekHold);
    if(next.phase!=TickAdvancePhase::Releasing)return fail(next.failure,"replacement_next_tick");
    seek_.witness.phase=SeekPhase::Advancing;
    return true;
}

bool Sc6ReplayHost::ObserveRollingHold(const InteriorWitness& held) noexcept
{
    auto& r=rolling_;auto& w=r.witness;
    r.telemetry.Backlog(unsigned(seek_retirement_pending_)+unsigned(particle_command_pending_.load())+
        unsigned(Sc6ReplayParticleCopy::capture_retirement_pending())+unsigned(held.surface_pending));
    if(w.phase==RollingPhase::Failed || w.phase==RollingPhase::Complete)return false;
    if(held.phase==InteriorPhase::Failed)return FailRolling(FailureCode::AdvanceFailed,"forward_failed");
    if(held.phase!=InteriorPhase::Holding || held.surface_pending)return false;
    if(w.phase==RollingPhase::Advancing) {
        if(held.tick==w.current && tick_advance_.phase==TickAdvancePhase::Releasing)return false;
        if(held.tick!=w.target)return FailRolling(FailureCode::AdvanceFailed,"forward_tick");
        if(held.boundary==PauseBoundary::SimulationTick) {
            const auto complete=CompleteApplicationToHold(this,[](void* context,const InteriorWitness& h) {
                return static_cast<Sc6ReplayHost*>(context)->ObserveRollingHold(h);
            });
            if(complete.phase!=TickAdvancePhase::Settling)FailRolling(complete.failure,"forward_tail_request");
            return false;
        }
        if(!held.application_idle || !held.engine_idle || !held.world_idle || held.pending_task || !held.arena_empty)
            return FailRolling(FailureCode::IllegalTransition,"forward_tail");
        w.current=held.tick;
        if(!PrepareRollingCheckpointSlot())return false;
        r.forward_us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-r.forward_started).count();
        CaptureWitness capture{};
        r.telemetry.Mark(ReplayRollingTelemetry::Phase::Capture,ReplayRollingTelemetry::Now());
        if(!CaptureOperation(CaptureAction::Begin,&capture))return FailRolling(capture.failure,"capture_begin");
        w.phase=RollingPhase::Capturing;return false;
    }
    if(w.phase==RollingPhase::Capturing) {
        CaptureWitness capture{};
        if(!CaptureOperation(CaptureAction::Read,&capture) || capture.phase==CapturePhase::Failed || capture.phase==CapturePhase::Cancelled)
            return FailRolling(capture.failure,"capture_read");
        if(capture.phase!=CapturePhase::Ready)return false;
        r.capture_us=capture.elapsed_us;
        CheckpointHandle image;
        if(!CaptureOperation(CaptureAction::Take,&capture,&image))return FailRolling(capture.failure,"capture_take");
        if(!StoreRollingCheckpoint(std::move(image)))return false;
        w.peak_bytes=(std::max)(w.peak_bytes,std::uint64_t(AdmissionBytes()));
        w.phase=RollingPhase::Ready;
        r.telemetry.Mark(ReplayRollingTelemetry::Phase::Retirement,ReplayRollingTelemetry::Now());
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] rolling checkpoint tick={} first={} capture_us={} owned_bytes={}\n"),
            w.current,w.first,r.capture_us,AdmissionBytes());
        return false;
    }
    if(w.phase==RollingPhase::Ready) {
        if(Sc6ReplayParticleCopy::capture_retirement_pending() || particle_command_pending_.load())return false;
        if(r.telemetry.active) {
            if(!r.telemetry.Complete(ReplayRollingTelemetry::Now(),true,!seek_retirement_pending_ && !held.surface_pending))return false;
            EmitRollingTelemetry();
        }
        if(w.current-w.first<7) {AdvanceRollingForward();return false;}
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] rolling timing begin ordinal={} tick={}\n"),w.cycles+1,w.current);
        if(!r.telemetry.Begin(w.cycles+1,w.current,ReplayRollingTelemetry::Now()))
            return FailRolling(FailureCode::CapacityExceeded,"telemetry_capacity");
        const auto& a=r.checkpoints[(w.current-7)%r.checkpoints.size()];
        if(!a || a->execution.tick!=w.current-7)return FailRolling(FailureCode::MissingSnapshot,"depth_seven_anchor");
        if(r.generations[(w.current-7)%r.generations.size()]!=w.checkpoint_generation || a->input_revision!=r.revision)
            return FailRolling(FailureCode::GenerationMismatch,"stale_history");
        if(r.schedule && r.next_correction<r.schedule->size() && r.schedule->row(r.next_correction).arrival_tick<=w.current) {
            const auto admitted=r.schedule->Admit(r.next_correction,w.current,checkpoint_session_,
                static_cast<std::uint64_t>(r.source.round)+1,w.source_revision,w.current-7,r.input_history);
            if(!admitted.status.ok())return FailRolling(admitted.status.code,"correction_admission");
            for(const auto& edit:r.schedule->edits(r.next_correction))
                if(a->source.cursor<0 || edit.sample<static_cast<unsigned>(a->source.cursor))
                    return FailRolling(FailureCode::GenerationMismatch,"correction_already_published_at_A");
            if(!HasRollingReplacementCapacity(Sc6ReplayParticleCopy::minimum_private_capture_bytes)) {
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] rolling correction rejected reason=replacement_capacity arrival={} revision={} generation={} minimum_new_gpu_bytes={} remaining_bytes={} A_published=false old_window_valid=true\n"),
                    w.current,w.source_revision,w.checkpoint_generation,
                    r.replacement.size()*Sc6ReplayParticleCopy::minimum_private_capture_bytes,AdmissionRemaining());
                return FailRolling(FailureCode::CapacityExceeded,"replacement_capacity_floor");
            }
            r.correcting=true;w.first_affected_tick=admitted.first_consumption;++w.corrections_admitted;
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] rolling correction admitted arrival={} revision={} generation={} first_consumption={} A={}\n"),
                w.current,w.source_revision,w.checkpoint_generation,w.first_affected_tick,w.current-7);
        }
        r.cycle_started=std::chrono::steady_clock::now();w.target=w.current;w.phase=RollingPhase::Seeking;
        if(simulation_->continuation().interval<a->execution.interval)return FailRolling(FailureCode::GenerationMismatch,"interval_order");
        r.cycle_intervals=simulation_->continuation().interval-a->execution.interval;
        SeekWitness seeking{};
        if(!SeekOperation(SeekAction::Begin,a.get(),w.current,&seeking,this,[](void* context,const SeekWitness& state,const InteriorWitness& h) {
            static_cast<Sc6ReplayHost*>(context)->ObserveRollingSeek(state,h);
        }))return FailRolling(FailureCode::RestorePreflightFailed,"restore_begin");
    }
    return false;
}

void Sc6ReplayHost::ObserveRollingSeek(const SeekWitness& state,const InteriorWitness& held) noexcept
{
    auto& r=rolling_;auto& w=r.witness;
    r.telemetry.Backlog(unsigned(seek_retirement_pending_)+unsigned(particle_command_pending_.load())+
        unsigned(Sc6ReplayParticleCopy::capture_retirement_pending())+unsigned(held.surface_pending));
    if(w.phase==RollingPhase::Failed)return;
    if(surface_event_ || particle_command_pending_.load(std::memory_order_acquire))return;
    w.peak_bytes=(std::max)(w.peak_bytes,std::uint64_t(AdmissionBytes()));
    if(state.phase==SeekPhase::Failed || state.phase==SeekPhase::Cancelled || state.preparation_fallbacks || state.execution_fallbacks) {
        FailRolling(state.failure==FailureCode::None?FailureCode::AdvanceFailed:state.failure,"transaction_failed");return;
    }
    if(state.origin!=w.current || state.target!=w.current || state.checkpoint!=w.current-7 || state.undo_tick!=w.current) {
        FailRolling(FailureCode::GenerationMismatch,"transaction_depth");return;
    }
    if(state.phase!=SeekPhase::Held || state.pending || held.surface_pending)return;
    if(held.tick!=w.current) {FailRolling(FailureCode::AdvanceFailed,"target_tick");return;}
    SeekWitness next{};
    if(!state.commit_decided) {
        if(r.correcting && !ValidateRollingReplacement()) {FailRolling(FailureCode::GenerationMismatch,"replacement_window_incomplete");return;}
        r.telemetry.Mark(ReplayRollingTelemetry::Phase::Application,ReplayRollingTelemetry::Now());
        if(!SeekOperation(SeekAction::CompleteTarget,nullptr,0,&next))FailRolling(FailureCode::AdvanceFailed,"target_tail_request");
        return;
    }
    if(held.boundary!=PauseBoundary::CompletedApplication || !held.application_idle || !held.engine_idle || !held.world_idle || held.pending_task) {
        FailRolling(FailureCode::IllegalTransition,"commit_tail");return;
    }
    r.telemetry.Mark(ReplayRollingTelemetry::Phase::Retirement,ReplayRollingTelemetry::Now());
    if(!SeekOperation(SeekAction::Release,nullptr,0,&next)) {FailRolling(FailureCode::PresentationFailed,"transaction_release");return;}
    if(next.release_result!=SeekReleaseResult::Completed)return;
    if(r.correcting && !CommitRollingReplacement()) {FailRolling(FailureCode::GenerationMismatch,"replacement_window_commit");return;}
    ++w.cycles;w.resimulated_ticks+=7;w.resimulated_intervals+=r.cycle_intervals;
    const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-r.cycle_started).count();
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] rolling cycle ordinal={} T={} A={} resimulated_ticks=7 committed=true tails_completed=true retirement_complete=true capture_us={} correction_us={} preceding_forward_us={} owned_bytes={} revision={} checkpoint_generation={}\n"),
        w.cycles,w.current,w.current-7,r.capture_us,elapsed,r.forward_us,AdmissionBytes(),w.source_revision,w.checkpoint_generation);
    if(w.cycles==w.requested) {
        r.telemetry.Terminal(ReplayRollingTelemetry::Now());EmitRollingTelemetry();
        // Drop only window references after commit. Generic hold retirement
        // joins GPU completion before the caller may release and resume.
        r.checkpoints={};r.revision.reset();w.phase=RollingPhase::Retiring;
        interior_context_=this;interior_observer_=[](void* context,const InteriorWitness& h) {
            auto& self=*static_cast<Sc6ReplayHost*>(context);auto& w=self.rolling_.witness;
            if(h.phase!=InteriorPhase::Holding || h.surface_pending || self.seek_retirement_pending_
                || self.particle_command_pending_.load() || Sc6ReplayParticleCopy::capture_retirement_pending())return false;
            w.phase=RollingPhase::Complete;
            auto& timing=self.rolling_.telemetry;
            timing.teardown_us=ReplayRollingTelemetry::Now()-timing.teardown_started;
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] rolling timing window protocol=1 warmup_us={} teardown_us={} samples={} storage_bytes={} backlog_scope=pending_owner_categories\n"),
                timing.warmup_us,timing.teardown_us,timing.size,sizeof(timing));
            return false;
        };
    } else AdvanceRollingForward();
}
