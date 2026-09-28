// Indexing owns this sequence; observers never provide resource cleanup.
bool Sc6ReplayHost::IndexBoundaryRequiresCompletion() const noexcept
{
    const auto tick=simulation_->continuation().tick;
    return interior_phase_==InteriorPhase::Armed && pause_boundary_==PauseBoundary::CompletedApplication
        && application_phase_==ApplicationPhase::Idle && engine_idle() && tick>=interior_target_
        && ((index_end_hold_==IndexEndHold::Arming && interior_target_==index_end_tick_)
            || (index_checkpoint_.phase==IndexCheckpointPhase::AwaitingBoundary && interior_target_==index_checkpoint_.requested_tick)
            || index_.witness().phase==ReplayTickIndex::Phase::Complete);
}

void Sc6ReplayHost::SelectInitialIndexCheckpoint() noexcept
{
    const bool first=index_checkpoint_selection_==IndexCheckpointSelection::FirstCombat;
    if((!first && index_checkpoint_selection_!=IndexCheckpointSelection::NextRound
            && index_checkpoint_selection_!=IndexCheckpointSelection::Done) || SessionExitRequested()
        || index_.witness().phase!=ReplayTickIndex::Phase::Recording || index_.witness().native_finish
        || (index_checkpoint_.phase!=IndexCheckpointPhase::Idle && index_checkpoint_.phase!=IndexCheckpointPhase::Complete)
        || !CanRetireSeekCheckpoint() || seek_retirement_pending_ || seek_retirement_failed_
        || surface_event_ || particle_command_pending_.load()
        || (interior_phase_!=InteriorPhase::Idle && interior_phase_!=InteriorPhase::Resumed)) return;
    const auto tick=simulation_->continuation().tick;
    ReplayTickIndex::Entry row{};
    // Native phase2's first six completed updates let initial HUD publication
    // settle. This is placement only: capture retains every existing owner,
    // eligibility, memory and lifetime check. An unsupported image is reported.
    // Never select an intro or silently label its uncheckpointed prefix seekable.
    if(tick==UINT64_MAX || !ReadIndexEntry(tick,&row) || !row.source_active || row.round_state!=2
        || row.round_tick<6 || (!first && row.round<index_checkpoint_round_)
        || EngineField<std::int32_t>(reinterpret_cast<void*>(image_base_),0x4846364)!=2) return;
    const IndexCheckpointHealth* replacement{};
    if(!first && row.round==index_checkpoint_round_) {
        // A lease cannot preserve an owner that native execution destroys.
        // After its normal retirement, try at most two replacement placements
        // in this round, at least30 ticks after observed invalidation. This is
        // a bounded placement policy, never evidence of snapshot restorability.
        if(index_checkpoint_replacement_attempts_>=2)return;
        for(const auto& health:index_checkpoint_health_) {
            if(health.session==checkpoint_session_ && health.phase==IndexCheckpointHealth::Phase::Invalid
                && health.captured_tick==index_checkpoint_.captured_tick && health.first_invalid
                && tick>=health.first_invalid && tick-health.first_invalid>=30) {
                replacement=&health;break;
            }
        }
        if(!replacement)return;
    }
    const auto accepted=RequestIndexCheckpoint(tick+1);
    if(!accepted.ok()) { index_.Fail(accepted.code);return; }
    if(replacement)++index_checkpoint_replacement_attempts_;
    else index_checkpoint_replacement_attempts_=0;
    index_checkpoint_selection_=first?IndexCheckpointSelection::FirstChosen:IndexCheckpointSelection::LastChosen;
    index_checkpoint_round_=row.round;
    // Keep the original anchors, then consider each remaining native round. Admission
    // accounts their simultaneous storage, B undo and in-flight retirement.
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] {} index checkpoint selected origin={} target={} native_phase_tick={} placement_owner=host\n"),
        first?STR("initial"):replacement?STR("replacement"):STR("next-round"),tick,tick+1,row.round_tick);
    if(replacement)RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] index replacement placement target={} rejected_checkpoint={} invalidated_at={} attempt={} retirement_idle=true\n"),
        tick+1,replacement->captured_tick,replacement->first_invalid,index_checkpoint_replacement_attempts_);
}

Status Sc6ReplayHost::RequestIndexCheckpoint(std::uint64_t tick) noexcept
{
    auto* self=active_;
    if(!self || GetCurrentThreadId()!=self->thread_)return Status::failure(FailureCode::WrongThread);
    if(self->index_.witness().phase!=ReplayTickIndex::Phase::Recording || self->index_.witness().native_finish || self->index_checkpoint_.pending()
        || self->index_checkpoint_.phase==IndexCheckpointPhase::Failed || self->depth_
        || self->SeekOwnsExecution() || self->historical_restore_ || self->checkpoint_restoring_
        || self->capture_operation_.phase!=CapturePhase::Idle || !self->CheckBinding()
        || self->surface_event_)
        return Status::failure(FailureCode::IllegalTransition);
    const InteriorObserver observer=[](void* context,const InteriorWitness& held) {
        return static_cast<Sc6ReplayHost*>(context)->DriveIndexCheckpointHold(held);
    };
    const auto current=self->simulation_->continuation().tick;
    if(tick!=current) {
        ReplayTickIndex::Entry row{};
        if(current==UINT64_MAX || tick!=current+1 || !ReadIndexEntry(current,&row)
            || !row.source_active || row.round_state!=2
            || EngineField<std::int32_t>(reinterpret_cast<void*>(self->image_base_),0x4846364)!=2
            || !self->ArmPause(tick,self,observer,PauseBoundary::CompletedApplication))
            return Status::failure(FailureCode::IllegalTransition);
        self->index_checkpoint_={IndexCheckpointPhase::AwaitingBoundary,FailureCode::None,tick,0};
        return Status::success();
    }
    if(self->interior_phase_!=InteriorPhase::Holding || self->pause_boundary_!=PauseBoundary::CompletedApplication
        || !self->CanRetireSeekCheckpoint())return Status::failure(FailureCode::IllegalTransition);
    self->interior_context_=self;
    self->interior_observer_=observer;
    self->index_checkpoint_={IndexCheckpointPhase::AwaitingCapture,FailureCode::None,tick,tick};
    return Status::success();
}

bool Sc6ReplayHost::ReadIndexCheckpoint(IndexCheckpointWitness* witness) noexcept
{
    auto* self=active_;
    if(!self || !witness || GetCurrentThreadId()!=self->thread_)return false;
    *witness=self->index_checkpoint_;return true;
}

bool Sc6ReplayHost::DriveIndexCheckpointHold(const InteriorWitness& held) noexcept
{
    auto& work=index_checkpoint_;
    const auto fail=[&](FailureCode code) {
        if(work.failure==FailureCode::None)work.failure=code;
        index_.Fail(code);work.phase=IndexCheckpointPhase::Failed;
        return false; // No release receipt on uncertain native ownership.
    };
    if(work.phase==IndexCheckpointPhase::Failed)return false;
    if(held.phase==InteriorPhase::Failed || !CheckBinding())return fail(FailureCode::GenerationMismatch);
    if(held.phase!=InteriorPhase::Holding || held.surface_pending)return false;
    if(held.boundary!=PauseBoundary::CompletedApplication || !held.application_idle || !held.engine_idle
        || !held.world_idle || !held.arena_empty || held.pending_task || held.tick!=work.requested_tick)
        return fail(FailureCode::IllegalTransition);
    if(work.captured_tick && work.captured_tick!=held.tick)return fail(FailureCode::GenerationMismatch);
    work.captured_tick=held.tick;
    if(work.phase==IndexCheckpointPhase::AwaitingBoundary)work.phase=IndexCheckpointPhase::AwaitingCapture;
    const bool cancelling=index_.witness().phase!=ReplayTickIndex::Phase::Recording;
    if(work.phase==IndexCheckpointPhase::Resuming && !cancelling)return true;
    if(cancelling && work.failure==FailureCode::None)work.failure=FailureCode::Cancelled;
    CaptureWitness capture{};
    if(!CaptureOperation(CaptureAction::Read,&capture,nullptr))return fail(FailureCode::CaptureFailed);
    if(cancelling) {
        // Captured images and in-flight commands retire through the same owner.
        if(capture.phase!=CapturePhase::Idle && capture.phase!=CapturePhase::Cancelled && capture.phase!=CapturePhase::Failed) {
            if(capture.phase!=CapturePhase::Retiring && !CaptureOperation(CaptureAction::Cancel,&capture,nullptr))
                return fail(FailureCode::CaptureFailed);
            return false;
        }
        if(capture.phase==CapturePhase::Cancelled || capture.phase==CapturePhase::Failed) {
            if(!CaptureOperation(CaptureAction::Release,&capture,nullptr))return fail(FailureCode::CaptureFailed);
        }
        work.phase=IndexCheckpointPhase::RetiringScratch;
    }
    if(work.phase==IndexCheckpointPhase::AwaitingCapture) {
        ReplayTickIndex::Entry row{};
        if(!ReadIndexEntry(held.tick,&row))return fail(FailureCode::GenerationMismatch);
        if(!CaptureOperation(CaptureAction::CheckTargetEligibility,&capture,nullptr))return fail(FailureCode::CaptureFailed);
        if(capture.failure==FailureCode::UnsupportedContent) {
            // No image or native publication occurred. Report this placement
            // as unsupported while preserving the still-recording index.
            work.failure=capture.failure;work.phase=IndexCheckpointPhase::Resuming;return true;
        }
        if(capture.failure!=FailureCode::None)return fail(capture.failure);
        if(!CaptureOperation(CaptureAction::Begin,&capture,nullptr))return fail(FailureCode::CaptureFailed);
        work.phase=IndexCheckpointPhase::Capturing;return false;
    }
    if(work.phase==IndexCheckpointPhase::Capturing) {
        if(capture.phase==CapturePhase::Failed || capture.phase==CapturePhase::Cancelled) {
            if(work.failure==FailureCode::None)work.failure=capture.failure==FailureCode::None?FailureCode::CaptureFailed:capture.failure;
            index_.Fail(work.failure);return false; // Next update owns cancellation/retirement.
        }
        if(capture.phase!=CapturePhase::Ready)return false;
        work.capture_elapsed_us=capture.elapsed_us;work.owned_bytes=capture.owned_bytes;
        ReplayTickIndex::Witness indexed{};
        if(!IndexOperation(IndexAction::RetainCapture,&indexed,0)) {
            if(work.failure==FailureCode::None)work.failure=indexed.failure==FailureCode::None?FailureCode::CaptureFailed:indexed.failure;
            index_.Fail(work.failure);return false;
        }
        work.phase=IndexCheckpointPhase::RetiringScratch;
    }
    if(work.phase==IndexCheckpointPhase::RetiringScratch) {
        const auto retirement_failure=[&] {
            if(work.failure==FailureCode::None)work.failure=FailureCode::PresentationFailed;
            index_.Fail(work.failure);
            // Retirement can be retried by its original native owner. Keep
            // the hold and pins until that owner acknowledges completion.
        };
        if(particle_copy_) {
            Sc6ReplayParticleCopy::Witness copied{};bool pending{};
            if(!ParticleCopyExperiment(ParticleCopyAction::Read,&copied,&pending)) {
                // Read still supplies the witness after a completed render
                // command reports failure. It does not authorize release.
                if(!particle_command_failed_.load())return fail(FailureCode::PresentationFailed);
                retirement_failure();
            }
            if(pending)return false;
            if(copied.phase!=Sc6ReplayParticleCopy::Phase::Released) {
                if(!ParticleCopyExperiment(ParticleCopyAction::Finish,&copied,&pending))retirement_failure();
                return false;
            }
        }
        const bool retire_index=index_.witness().phase!=ReplayTickIndex::Phase::Recording;
        if(retire_index && index_.witness().phase!=ReplayTickIndex::Phase::Releasing && index_.witness().phase!=ReplayTickIndex::Phase::Empty) {
            ReplayTickIndex::Witness indexed{};
            if(!IndexOperation(IndexAction::Release,&indexed,0))return fail(FailureCode::PresentationFailed);
            return false;
        }
        if(retire_index && index_.witness().phase==ReplayTickIndex::Phase::Releasing)return false;
        work.phase=IndexCheckpointPhase::Resuming;
        return true; // AdvanceInteriorHold still joins deferred GPU retirement.
    }
    return false;
}

void Sc6ReplayHost::FinishIndexCheckpointResume() noexcept
{
    if(index_checkpoint_.phase!=IndexCheckpointPhase::Resuming || interior_phase_!=InteriorPhase::Resumed
        || surface_event_ || particle_command_pending_.load() || Sc6ReplayParticleCopy::capture_retirement_pending()
        || capture_operation_.phase!=CapturePhase::Idle || seek_retirement_pending_)return;
    if(index_.witness().phase==ReplayTickIndex::Phase::Releasing)return;
    if(index_.witness().phase==ReplayTickIndex::Phase::Cancelled || index_.witness().phase==ReplayTickIndex::Phase::Failed) {
        ReplayTickIndex::Witness released{};
        if(!IndexOperation(IndexAction::Release,&released,0)) {
            index_checkpoint_.phase=IndexCheckpointPhase::Failed;
            if(index_checkpoint_.failure==FailureCode::None)index_checkpoint_.failure=FailureCode::PresentationFailed;
        } else if(index_checkpoint_.failure==FailureCode::None)index_checkpoint_.failure=FailureCode::Cancelled;
        return;
    }
    index_checkpoint_.phase=IndexCheckpointPhase::Complete;
    const bool first=index_checkpoint_selection_==IndexCheckpointSelection::FirstChosen;
    const bool later=index_checkpoint_selection_==IndexCheckpointSelection::LastChosen;
    if((first || later) && index_.witness().phase==ReplayTickIndex::Phase::Recording) {
        if(index_checkpoint_.failure!=FailureCode::None) {
            // Only pre-capture eligibility can skip an optional placement.
            // Capture/ownership/capacity failures retain their normal guards.
            if(later && index_checkpoint_.failure==FailureCode::UnsupportedContent) {
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] optional index checkpoint unsupported tick={} original_anchor_retained=true\n"),index_checkpoint_.requested_tick);
                index_checkpoint_selection_=index_checkpoint_round_+1<index_source_.recording_count
                    ?IndexCheckpointSelection::NextRound:IndexCheckpointSelection::Done;
            } else index_.Fail(index_checkpoint_.failure);
        } else index_checkpoint_selection_=index_checkpoint_round_+1<index_source_.recording_count
                ?IndexCheckpointSelection::NextRound:IndexCheckpointSelection::Done;
    }
}
