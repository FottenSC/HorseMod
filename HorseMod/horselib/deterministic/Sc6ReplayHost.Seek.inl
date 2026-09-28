// Request orchestration remains in the application owner. GPU completion,
// native publication and undo remain the existing historical transaction's
// responsibility; this layer never writes checkpoint contents itself.
Sc6ReplayHost::CheckpointHandle Sc6ReplayHost::EarlierSeekCheckpoint(std::uint64_t before,std::uint64_t target) const noexcept
{
    CheckpointHandle selected;
    for(const auto& weak:retained_checkpoints_) {
        auto candidate=weak.lock();
        if(candidate && candidate->historical_target_shape_supported() && candidate->session==checkpoint_session_
            && candidate->boundary==PauseBoundary::CompletedApplication && !candidate->task && !candidate->event
            && candidate->execution.phase==Sc6ReplayExecutor::Phase::Idle && candidate->gpu && candidate->surface
            && candidate->execution.tick<before && candidate->execution.tick<=target
            && candidate->execution.tick<=simulation_->continuation().tick && candidate->vfx.retained_owners_live()
            && (!selected || candidate->execution.tick>selected->execution.tick)) selected=std::move(candidate);
    }
    return selected;
}

bool Sc6ReplayHost::CanRetireSeekCheckpoint() const noexcept
{
    // The hold owns its Draw/Release completion, even with an idle engine.
    // Retirement must not consume that event while the hold is Releasing.
    return application_phase_==ApplicationPhase::Idle && engine_idle() && world_idle()
        && !engine_post_deferred_
        && (interior_phase_==InteriorPhase::Idle || interior_phase_==InteriorPhase::Resumed
            || (interior_phase_==InteriorPhase::Holding && pause_boundary_==PauseBoundary::CompletedApplication
                && !historical_restore_ && !checkpoint_restoring_));
}

bool Sc6ReplayHost::SeekOperation(SeekAction action,const Checkpoint* checkpoint,std::uint64_t target,
    SeekWitness* output,void* context,SeekObserver observer) noexcept
{
    auto* self=active_;
    if(!self || !output || GetCurrentThreadId()!=self->thread_) return false;
    *output=self->seek_.witness;
    if(action==SeekAction::Release) output->release_result=SeekReleaseResult::Rejected;
    if(action!=SeekAction::Begin && action!=SeekAction::Read && action!=SeekAction::Cancel && action!=SeekAction::Release
        && action!=SeekAction::InjectAdvanceFailure && action!=SeekAction::CompleteTarget && action!=SeekAction::Step
        && action!=SeekAction::InjectSettlementFailure && action!=SeekAction::InjectPreparationFailure && action!=SeekAction::ResumeTarget)
        return false;
    if(action==SeekAction::Read) return true;
    if(self->historical_restore_ && self->historical_restore_->material_failed) return false;
    if(self->SessionExitRequested() && (action==SeekAction::Begin || action==SeekAction::Step || action==SeekAction::ResumeTarget)) return false;
    if(self->depth_) return false;
    auto& state=self->seek_;
    auto& witness=state.witness;
    if(action==SeekAction::InjectPreparationFailure) {
        if(witness.phase!=SeekPhase::Preparing || !witness.automatic_selection || state.cancel
            || state.preparation_failure_checkpoint || target!=witness.checkpoint || witness.preparation_fallbacks
            || !self->CheckBinding() || self->checkpoint_restoring_) return false;
        state.preparation_failure_checkpoint=target;return true;
    }
    if(action==SeekAction::Release && witness.release_result==SeekReleaseResult::Completed) {
        // Acknowledge the completed retirement. Keep an idempotent value-only
        // receipt; a later Begin replaces it with its own operation witness.
        *output=witness;state={};state.witness.release_result=SeekReleaseResult::Completed;
        self->interior_context_=nullptr;self->interior_observer_=nullptr;
        return true;
    }
    if(action==SeekAction::Release && witness.release_result==SeekReleaseResult::AcceptedPending
        && witness.pending && witness.phase!=SeekPhase::Failed) {
        *output=witness;return true;
    }
    if(action==SeekAction::InjectSettlementFailure)
        return witness.phase==SeekPhase::Restored && self->ArmSettlementFailure(target);
    if(action==SeekAction::Step) {
        if(!self->index_.AllowsPlaybackFrom(self->simulation_->continuation().tick)) return false;
        if(witness.phase!=SeekPhase::Held || witness.pending || witness.commit_decided
            || self->simulation_->continuation().tick==UINT64_MAX) return false;
        const auto prior=self->seek_driving_;self->seek_driving_=true;
        const auto stepped=self->StepHistoricalExecution();self->seek_driving_=prior;
        if(!stepped.ok()) return false;
        witness.target=self->simulation_->continuation().tick+1;
        witness.prefix_us=witness.engine_us=witness.tail_us=witness.frame_sync_us=0;
        witness.phase=SeekPhase::Advancing;witness.pending=true;
        *output=witness;return true;
    }
    if(action==SeekAction::InjectAdvanceFailure) {
        if(witness.phase!=SeekPhase::Restored || state.advance_failure_tick
            || !self->HistoricalExecutionAdmitted() || !self->CheckBinding()
            || self->interior_phase_!=InteriorPhase::Holding || self->pause_boundary_!=PauseBoundary::CompletedApplication
            || target<=self->simulation_->continuation().tick || target>=witness.target) return false;
        state.advance_failure_tick=target;
        return true;
    }
    if(action==SeekAction::Begin) {
        if(self->index_.witness().phase==ReplayTickIndex::Phase::Recording
            || self->index_.witness().phase==ReplayTickIndex::Phase::Releasing) return false;
        if(self->index_.witness().phase==ReplayTickIndex::Phase::Complete && target>=self->index_.witness().entries) return false;
        // A held target already owns its exact native continuation. An
        // identical request requires neither a historical image nor live
        // historical particle owners. Do not replace callbacks, retire pins,
        // complete pending world work, or publish another transaction here.
        const bool retained_hold=self->historical_restore_
            && self->historical_restore_->witness.ownership_phase==ReplaySeekOwnership::Phase::CSettled;
        // An explicit earlier checkpoint from an idle operation is a rollback
        // request: restore A and resimulate to the original B coordinate. It
        // still resolves through the owned registry and retains complete B.
        // Automatic/UI same-tick requests keep their existing no-work receipt.
        const bool resimulate_to_origin=target==self->simulation_->continuation().tick
            && checkpoint && witness.phase==SeekPhase::Idle;
        if(target==self->simulation_->continuation().tick && !resimulate_to_origin)
            return witness.phase==SeekPhase::Held && !witness.pending && witness.target==target
                && !self->seek_retirement_pending_ && ((!self->historical_restore_ && !self->checkpoint_restoring_) || retained_hold)
                && self->capture_operation_.phase==CapturePhase::Idle && self->CheckBinding()
                && self->interior_phase_==InteriorPhase::Holding;
        // Selection pins only a host-accounted immutable checkpoint. A
        // displaced owner transfers to retirement until its application tail
        // and render work complete; selection never releases native leases.
        CheckpointHandle selected;
        const bool automatic_selection=checkpoint==nullptr;
        std::uint64_t rejected_checkpoint{};
        bool retained_owner_rejected=false;
        if(!checkpoint) {
            // Value-only invalidation receipts survive native retirement. They
            // explain a fallback without keeping destroyed checkpoint owners.
            for(const auto& health:self->index_checkpoint_health_) {
                if(health.session==self->checkpoint_session_ && health.phase==IndexCheckpointHealth::Phase::Invalid
                    && health.captured_tick<=target && health.captured_tick<=self->simulation_->continuation().tick
                    && (!retained_owner_rejected || health.captured_tick>rejected_checkpoint)) {
                    rejected_checkpoint=health.captured_tick;retained_owner_rejected=true;
                }
            }
            for(const auto& weak:self->retained_checkpoints_) {
                const auto candidate=weak.lock();
                if(candidate && candidate->historical_target_shape_supported() && candidate->session==self->checkpoint_session_
                    && candidate->boundary==PauseBoundary::CompletedApplication && !candidate->task && !candidate->event
                    && candidate->execution.phase==Sc6ReplayExecutor::Phase::Idle && candidate->gpu && candidate->surface
                    && candidate->execution.tick<=target && candidate->execution.tick<=self->simulation_->continuation().tick) {
                    if(!candidate->vfx.retained_owners_live()) {
                        self->DiagnoseCheckpointOwner(*candidate);
                        if(!retained_owner_rejected || candidate->execution.tick>rejected_checkpoint)
                            rejected_checkpoint=candidate->execution.tick;
                        retained_owner_rejected=true;
                    } else if(!selected || candidate->execution.tick>selected->execution.tick) selected=candidate;
                }
            }
            checkpoint=selected.get();
        } else {
            // Public handles may be expired or arbitrary addresses. Resolve
            // ownership before inspecting any checkpoint member, including
            // enable_shared_from_this. Address equality alone is safe here.
            selected=self->ResolveOwnedCheckpoint(checkpoint);
            if(!selected) return false;
        }
        // Replacement is admitted only after a completed
        // request; an in-flight publication is never overwritten by UI input.
        const bool replacing=(witness.phase==SeekPhase::Held || witness.phase==SeekPhase::Cancelled)
            && !witness.pending;
        if((self->SeekOwnsExecution() && !replacing) || self->seek_retirement_pending_ || self->historical_restore_ || self->checkpoint_restoring_
            || self->capture_operation_.phase!=CapturePhase::Idle || !self->CheckBinding()
            || self->interior_phase_!=InteriorPhase::Holding || !checkpoint || !checkpoint->historical_target_shape_supported()
            || checkpoint->session!=self->checkpoint_session_ || !checkpoint->gpu || !self->RestoreCheckpointDisplay(*checkpoint)
            || checkpoint->boundary!=PauseBoundary::CompletedApplication || checkpoint->task || checkpoint->event
            || checkpoint->execution.phase!=Sc6ReplayExecutor::Phase::Idle
            || checkpoint->execution.tick>target
            || (resimulate_to_origin && checkpoint->execution.tick>=target)
            || self->AdmissionBytes()>self->memory_limit_) return false;
        auto pin=std::move(selected);
        if(!pin || pin.get()!=checkpoint) return false;
        auto phase=SeekPhase::Preparing;
        if(self->pause_boundary_==PauseBoundary::SimulationTick) {
            self->seek_driving_=true;
            const auto completed=CompleteApplicationToHold(self,&ObserveSeekHold);
            self->seek_driving_=false;
            if(completed.phase!=TickAdvancePhase::Settling) return false;
            phase=SeekPhase::CompletingApplication;
        } else if(self->pause_boundary_!=PauseBoundary::CompletedApplication || !self->engine_idle()
            || !self->executor_.idle() || self->application_phase_!=ApplicationPhase::Idle) return false;
        // All rejecting preflight runs before replacing the previous witness.
        // Transfer the displaced pin without releasing it at an interior tick.
        if(state.checkpoint && state.checkpoint!=pin) {
            self->released_seek_checkpoint_=std::move(state.checkpoint);
            self->seek_retirement_pending_=true;
        }
        state={};state.checkpoint=std::move(pin);state.context=context;state.observer=observer;
        witness={phase,FailureCode::None,self->simulation_->continuation().tick,
            checkpoint->execution.tick,target,self->simulation_->continuation().tick,self->AdmissionBytes(),true};
        witness.automatic_selection=automatic_selection;
        witness.rejected_checkpoint=rejected_checkpoint;
        witness.retained_owner_rejected=retained_owner_rejected;
        self->interior_context_=self;self->interior_observer_=&ObserveSeekHold;
        self->interior_resume_requested_=false;
    } else if(action==SeekAction::Cancel) {
        if(witness.phase==SeekPhase::Idle || (witness.phase==SeekPhase::Held && !self->historical_restore_)
            || witness.commit_decided) return false;
        if(witness.phase==SeekPhase::Cancelled) return true;
        // A failed wrapper cannot decide whether native B remains recoverable.
        // Delegate that admission to the transaction, including its dirty-state,
        // lifetime and binding checks. Never clear those checks here.
        if(witness.phase==SeekPhase::Failed && !self->historical_restore_) {
            // A retry may lose its earlier checkpoint after complete B recovery
            // and transaction release. Acknowledge that existing receipt only;
            // absence of a transaction is never itself evidence of recovery.
            if(!witness.original_recovered || witness.pending || self->checkpoint_restoring_
                || !self->CheckBinding() || self->simulation_->continuation().tick!=witness.undo_tick
                || !self->CanRetireSeekCheckpoint()) return false;
            state.cancel=true;state.restore_requested=false;
            witness.phase=SeekPhase::Cancelled;witness.release_result=SeekReleaseResult::NotRequested;
            *output=witness;return true;
        }
        if(self->historical_restore_ && !self->corrected_capture_) {
            const auto prior=self->seek_driving_;self->seek_driving_=true;
            RestoreOperationWitness cancelling{};
            const bool accepted=RestoreOperation(RestoreOperationAction::Cancel,nullptr,&cancelling);
            self->seek_driving_=prior;
            if(!accepted) return false;
        }
        state.cancel=true;
        witness.release_result=SeekReleaseResult::NotRequested;
        if(witness.phase!=SeekPhase::CompletingApplication) witness.phase=SeekPhase::Recovering;
        // Cancellation is accepted work, even when requested from a settled
        // hold whose previous receipt was not pending. Only AdvanceSeek can
        // acknowledge completed B recovery and retirement.
        witness.pending=true;
    } else if(action==SeekAction::Release || action==SeekAction::CompleteTarget || action==SeekAction::ResumeTarget) {
        if(action!=SeekAction::ResumeTarget && self->tick_advance_.phase==TickAdvancePhase::CompletionBlocked) {
            *output=witness;output->release_result=SeekReleaseResult::RequiresResume;return false;
        }
        if(action==SeekAction::ResumeTarget && !self->index_.AllowsPlaybackFrom(self->simulation_->continuation().tick)) return false;
        if(witness.phase==SeekPhase::Held && self->historical_restore_ && !witness.pending) {
            const auto prior=self->seek_driving_;self->seek_driving_=true;
            RestoreOperationWitness completing{};
            const bool accepted=RestoreOperation(action==SeekAction::ResumeTarget?RestoreOperationAction::ResumeTarget:RestoreOperationAction::CompleteTarget,nullptr,&completing);
            self->seek_driving_=prior;
            if(accepted) {
                witness.pending=true;
                if(action==SeekAction::Release || action==SeekAction::ResumeTarget) witness.release_result=SeekReleaseResult::AcceptedPending;
            }
            *output=witness;
            if(action==SeekAction::Release && !accepted) output->release_result=SeekReleaseResult::Rejected;
            return accepted; // Accepted completion retains B until native tails finish.
        }
        // Explicit completion preserves the held request, callback and checkpoint
        // pin, so a subsequent Begin can capture a fresh B without a Resume.
        if(action==SeekAction::CompleteTarget)
            return witness.phase==SeekPhase::Held && !witness.pending && !self->historical_restore_
                && !self->checkpoint_restoring_ && self->CheckBinding()
                && self->pause_boundary_==PauseBoundary::CompletedApplication;
        if((witness.phase!=SeekPhase::Held && witness.phase!=SeekPhase::Cancelled && witness.phase!=SeekPhase::Failed)
        || witness.pending || self->seek_retirement_pending_ || self->historical_restore_ || self->checkpoint_restoring_
            || (self->surface_event_ && self->surface_command_!=SurfaceCommand::Draw)
            || self->particle_command_pending_.load()
            || (self->particle_copy_ && self->particle_copy_->blocks_resume())) return false;
        if(self->pause_boundary_==PauseBoundary::SimulationTick) {
            const auto prior=self->seek_driving_;self->seek_driving_=true;
            const auto completion=CompleteApplicationToHold(self,&ObserveSeekHold);
            self->seek_driving_=prior;
            if(completion.phase!=TickAdvancePhase::Settling) return false;
            witness.release_result=SeekReleaseResult::AcceptedPending;
            witness.pending=true;*output=witness;return true;
        }
        // GC leases and native render references cannot retire at an unfinished
        // world boundary. Transfer the pin without allocation; the application
        // owner drains it only after the real engine/world/application tails.
        self->released_seek_checkpoint_=std::move(state.checkpoint);
        self->seek_retirement_pending_=self->released_seek_checkpoint_!=nullptr;
        witness.release_result=SeekReleaseResult::AcceptedPending;
        witness.pending=true;
        *output=witness;
        return true;
    }
    *output=witness;return true;
}

bool Sc6ReplayHost::ObserveSeekHold(void* context,const InteriorWitness& held)
{
    auto& self=*static_cast<Sc6ReplayHost*>(context);
    if(held.ui_cancel_requests!=self.interior_ui_cancel_baseline_
        && !(self.seek_.witness.phase==SeekPhase::Failed && held.surface_pending)) {
        self.interior_ui_cancel_baseline_=held.ui_cancel_requests;
        SeekWitness cancelled{};
        const bool accepted=SeekOperation(SeekAction::Cancel,nullptr,0,&cancelled);
        self.TraceSeekCancellation(held.tick,accepted);
        // The native transaction owns admission/recovery. A click is never
        // permission to release an unresolved operation or start playback.
    }
    if(self.seek_.witness.phase==SeekPhase::Held && !self.seek_.witness.pending
        && self.historical_restore_ && held.ui_step_requests!=self.interior_ui_step_baseline_) {
        SeekWitness stepped{};
        if(SeekOperation(SeekAction::Step,nullptr,0,&stepped)) {
            self.interior_ui_step_baseline_=held.ui_step_requests;
            self.TraceSeekStep(held.tick,stepped.target);
        }
        // Notify the observer of the accepted request at its still-held origin
        // before the next application update can execute the native step.
    }
    // Observers report progress; they do not own or disable UI commands.
    // Explicit Resume also acknowledges a failed request that never acquired
    // (or no longer retains) a native transaction. Release still owns all
    // admission checks, pending tails and deferred checkpoint retirement.
    // A dirty failure must recover B first; a failed request cannot single-step.
    const bool clean_failed=self.seek_.witness.phase==SeekPhase::Failed
        && !self.historical_restore_ && !self.checkpoint_restoring_;
    if((self.seek_.witness.phase==SeekPhase::Held || self.seek_.witness.phase==SeekPhase::Cancelled || clean_failed)
        && !self.seek_.witness.pending) {
        const bool can_advance=self.index_.AllowsPlaybackFrom(held.tick);
        const bool step=can_advance && !clean_failed && held.ui_step_requests!=self.interior_ui_step_baseline_;
        const bool resume=can_advance && (held.ui_resume_requests!=self.interior_ui_resume_baseline_ || self.interior_resume_requested_);
        if(step || resume) {
            SeekWitness released{};
            if(!SeekOperation(resume && self.historical_restore_?SeekAction::ResumeTarget:SeekAction::Release,nullptr,0,&released)
                || released.release_result!=SeekReleaseResult::Completed) return false;
            if(step && held.tick!=UINT64_MAX) {
                self.interior_ui_step_baseline_=held.ui_step_requests;
                const auto requested=self.AdvanceToTick(held.tick+1,nullptr,nullptr);
                // Rejection keeps the current hold. Never turn a failed step
                // into an implicit Resume; Release retains the native owner.
                (void)requested;
                return false;
            }
            if(resume)self.TraceSeekUiResume(held.tick);
            return resume;
        }
    }
    if(self.seek_.observer) {
        // The callback may Release the operation. Its progress observation must
        // not alias the state it can retire or replace during this invocation.
        const auto progress=self.seek_.witness;
        try {self.seek_.observer(self.seek_.context,progress,held);}
        catch(...) {
            self.seek_.witness.failure=FailureCode::AdvanceFailed;
            self.seek_.witness.phase=SeekPhase::Failed;
            self.seek_.witness.pending=self.historical_restore_!=nullptr;
        }
    }
    return false; // Readiness never starts playback.
}

void Sc6ReplayHost::AdvanceSeek() noexcept
{
    auto& state=seek_;auto& witness=state.witness;
    // Material failure retains complete B, provisional checkpoints and native
    // in-flight owners. Do not turn it into ordinary capture Cancel/Release,
    // drop rolling replacement pins, or replace its cause with binding failure.
    if(historical_restore_ && historical_restore_->material_failed)return;
    if(state.cancel && rolling_.correcting
        && std::any_of(rolling_.replacement.begin(),rolling_.replacement.end(),[](const auto& image){return bool(image);})) {
        if(historical_restore_ && (surface_event_ || particle_command_pending_.load(std::memory_order_acquire)
            || !CheckHistoricalMaterialBoundary()))return;
        rolling_.replacement={};rolling_.replacement_generations={};rolling_.rebuild_complete=false;
        corrected_capture_=true;capture_operation_.phase=CapturePhase::Retiring;capture_operation_.pending=true;
    }
    if(corrected_capture_) {
        // A provisional copy may still reference C's native resources. Retire
        // it before recovery can destroy C or publish B. The same rule holds
        // when cancellation arrives after its CPU checkpoint was assembled.
        if(capture_operation_.failure!=FailureCode::None) {
            state.cancel=true;
            if(witness.failure==FailureCode::None)witness.failure=capture_operation_.failure;
        }
        if(state.cancel) {
            CaptureWitness capture{};
            if(capture_operation_.phase==CapturePhase::Cancelled || capture_operation_.phase==CapturePhase::Failed)
                CaptureOperation(CaptureAction::Release,&capture);
            else if(capture_operation_.phase!=CapturePhase::Retiring)
                CaptureOperation(CaptureAction::Cancel,&capture);
        } else if(capture_operation_.phase==CapturePhase::Ready) {
            DriveRollingReplacement();
        }
        return;
    }
    if(witness.release_result==SeekReleaseResult::AcceptedPending && !historical_restore_
        && (witness.phase==SeekPhase::Held || witness.phase==SeekPhase::Cancelled || witness.phase==SeekPhase::Failed)) {
        if(state.checkpoint) {
            // Target completion is finished; now transfer the pin to the same
            // deferred retirement owner used by explicit completed releases.
            if(witness.pending) {
                if(tick_advance_.phase==TickAdvancePhase::Failed) {
                    witness.failure=tick_advance_.failure;witness.phase=SeekPhase::Failed;
                    witness.release_result=SeekReleaseResult::Rejected;return;
                }
                if(tick_advance_.phase!=TickAdvancePhase::Settled || !CanRetireSeekCheckpoint()) return;
                witness.pending=false;
            }
            SeekWitness releasing{};
            SeekOperation(SeekAction::Release,nullptr,0,&releasing);
            return;
        }
        if(seek_retirement_pending_ && !AdvanceSeekRetirement()) {
            if(seek_retirement_failed_) {
                witness.failure=FailureCode::PresentationFailed;witness.phase=SeekPhase::Failed;
                witness.release_result=SeekReleaseResult::Rejected;
            }
            return;
        }
        witness.release_result=SeekReleaseResult::Completed;witness.pending=false;
        return; // Notify the retained observer before it acknowledges release.
    }
    if(witness.phase==SeekPhase::Idle || (witness.phase==SeekPhase::Held && !historical_restore_) || witness.phase==SeekPhase::Cancelled) return;
    if(witness.phase==SeekPhase::Failed) {
        // Keep the native failure witness until explicit cancellation. A clean
        // preparation failure can acknowledge unchanged B after its retirement;
        // auto-releasing here used to destroy that recovery evidence before a
        // later caller could cancel. Dirty failures retain the same safeguards.
        witness.pending=historical_restore_!=nullptr || seek_retirement_pending_ || surface_event_ || particle_command_pending_.load();
        return;
    }
    const auto fail=[&](FailureCode code) {
        witness.failure=code;witness.phase=SeekPhase::Failed;
        witness.pending=historical_restore_!=nullptr || seek_retirement_pending_ || surface_event_ || particle_command_pending_.load();
    };
    if(seek_retirement_pending_ && witness.phase!=SeekPhase::CompletingApplication) {
        if(!AdvanceSeekRetirement()) {
            if(seek_retirement_failed_) fail(FailureCode::PresentationFailed);
            return;
        }
    }
    if(surface_event_ || particle_command_pending_.load(std::memory_order_acquire))return;
    witness.owned_bytes=AdmissionBytes();
    const bool native_recovery=historical_restore_
        && (historical_restore_->witness.phase==RestoreOperationPhase::Recovering
            || historical_restore_->witness.phase==RestoreOperationPhase::Recovered);
    // Capacity failure forbids new forward work. It must not prevent the
    // existing undo owner from recovering/releasing already admitted storage.
    // Native recovery keeps its own allocation, binding and dirty-state checks.
    if(witness.owned_bytes>memory_limit_ && !state.cancel && !native_recovery)
    {if(rolling_.correcting)state.cancel=true;fail(FailureCode::CapacityExceeded);return;}
    if(!CheckBinding() || interior_phase_==InteriorPhase::Failed) {fail(FailureCode::GenerationMismatch);return;}
    if(witness.phase==SeekPhase::CompletingApplication) {
        if(tick_advance_.phase==TickAdvancePhase::Failed) {fail(tick_advance_.failure);return;}
        if(tick_advance_.phase==TickAdvancePhase::CompletionBlocked) {fail(FailureCode::IllegalTransition);return;}
        if(tick_advance_.phase!=TickAdvancePhase::Settled || surface_event_) return;
        witness.undo_tick=simulation_->continuation().tick;
        witness.phase=SeekPhase::Preparing;
        return;
    }
    RestoreOperationWitness restore{};
    if(witness.phase==SeekPhase::RetryingPreparation || witness.phase==SeekPhase::RetryingExecution) {
        const bool executed=witness.phase==SeekPhase::RetryingExecution;
        // Both routes require complete B and release of the old transaction
        // before replacing its pin. Execution retries additionally retain the
        // discarded traversal accounting. User Cancel takes precedence.
        if(!historical_restore_) {fail(FailureCode::UndoFailed);return;}
        restore=historical_restore_->witness;
        if(restore.phase==RestoreOperationPhase::Failed) {fail(restore.failure==FailureCode::None?FailureCode::UndoFailed:restore.failure);return;}
        if(restore.phase!=RestoreOperationPhase::Recovered || restore.pending) return;
        if(!restore.original_recovered || simulation_->continuation().tick!=witness.undo_tick
            || restore.original_tick!=witness.undo_tick || restore.commit_decided) {fail(FailureCode::UndoFailed);return;}
        auto earlier=EarlierSeekCheckpoint(witness.checkpoint,witness.target);
        if(executed && earlier && !CanRetryHistoricalInputs(*earlier)) earlier.reset();
        if(!RestoreOperation(RestoreOperationAction::Release,nullptr,&restore)) return;
        if(!earlier) {
            // Preserve completed recovery even though no next attempt can be
            // admitted. The caller can acknowledge cancellation or release the
            // remaining checkpoint pin through normal deferred retirement.
            state.restore_requested=false;witness.original_recovered=true;
            witness.recovered_execution_ticks=restore.executed_ticks;
            witness.recovered_execution_intervals=restore.executed_intervals;
            fail(executed?witness.last_execution_failure:witness.last_preparation_failure);return;
        }
        const auto rejected=witness.checkpoint;
        released_seek_checkpoint_=std::move(state.checkpoint);
        seek_retirement_pending_=released_seek_checkpoint_!=nullptr;
        state.checkpoint=std::move(earlier);state.restore_requested=false;
        witness.checkpoint=state.checkpoint->execution.tick;
        if(executed) {
            ++witness.execution_fallbacks;
            witness.fallback_execution_ticks+=restore.executed_ticks;
            witness.fallback_execution_intervals+=restore.executed_intervals;
            TraceSeekExecutionFallback(rejected,witness.checkpoint,restore);
        } else {
            ++witness.preparation_fallbacks;
            TraceSeekPreparationFallback(rejected,witness.checkpoint,witness.last_preparation_failure);
        }
        witness.phase=SeekPhase::Preparing;witness.pending=true;
        return; // The next update first drains the displaced checkpoint owner.
    }
    if(state.cancel) {
        if(!state.restore_requested) {
            witness.phase=SeekPhase::Cancelled;witness.pending=false;
            witness.original_recovered=simulation_->continuation().tick==witness.undo_tick;return;
        }
        if(historical_restore_ && historical_restore_->witness.phase==RestoreOperationPhase::Failed) {
            const auto& preparation=*historical_restore_;
            const auto& receipt=preparation.witness;
            // Preparation reports its accepted cancellation after retiring
            // submitted render work and recovering excluded B bodies. Finish
            // that existing request through native Cancel's unchanged-B checks.
            // A failed undo is different: it still requires a new caller action,
            // and rejection below transitions to Failed without automatic retry.
            if(receipt.failure!=FailureCode::Cancelled || receipt.pending || receipt.commit_decided
                || receipt.ownership_phase || !preparation.preparation_retiring
                || preparation.preparing || preparation.execution) {
                fail(receipt.failure==FailureCode::None?FailureCode::UndoFailed:receipt.failure);return;
            }
        }
        if(!RestoreOperation(RestoreOperationAction::Cancel,nullptr,&restore)) {fail(FailureCode::UndoFailed);return;}
        witness.phase=SeekPhase::Recovering;
    }
    if(witness.phase==SeekPhase::Preparing && !state.restore_requested) {
        if(surface_event_ || particle_command_pending_.load()) return;
        if(witness.automatic_selection) {
            // An expired later image still consumes B's budget even when it
            // was ineligible for this target. Retire invalid index pins only;
            // selection/rejection witnesses keep their original meaning.
            const auto retired=RetireRejectedIndexCheckpoints();
            if(!retired.ok()) {fail(retired.code);return;}
            if(seek_retirement_pending_)return;
        }
        if(!RestoreOperation(RestoreOperationAction::Request,state.checkpoint.get(),&restore)) {
            fail(restore.failure==FailureCode::None?FailureCode::RestorePreflightFailed:restore.failure);return;
        }
        state.restore_requested=true;return;
    }
    if(state.restore_requested && historical_restore_) {
        restore=historical_restore_->witness;
        witness.commit_decided=restore.commit_decided;
        if(!restore.commit_decided && (restore.phase==RestoreOperationPhase::Recovering
            || restore.phase==RestoreOperationPhase::Recovered)) {
            state.cancel=true;witness.phase=SeekPhase::Recovering;
            if(witness.failure==FailureCode::None) witness.failure=restore.failure;
        }
        if(restore.phase==RestoreOperationPhase::Failed) {
            // This one failure has a demonstrated complete-B recovery route.
            // No other publication/settlement failure becomes an implicit retry.
            if(witness.phase==SeekPhase::Advancing && witness.automatic_selection && !state.cancel
                && !restore.pending && !restore.commit_decided && restore.failure==FailureCode::UnsupportedContent
                && restore.ownership_phase==ReplaySeekOwnership::Phase::FailedRecoverable
                && restore.participant && std::string_view(restore.participant)=="execution_protected_particle_lifetime") {
                const auto earlier=EarlierSeekCheckpoint(witness.checkpoint,witness.target);
                if(earlier && CanRetryHistoricalInputs(*earlier)) {
                    if(!RestoreOperation(RestoreOperationAction::Cancel,nullptr,&restore)) {fail(FailureCode::UndoFailed);return;}
                    witness.last_execution_failure=FailureCode::UnsupportedContent;
                    witness.phase=SeekPhase::RetryingExecution;witness.pending=true;return;
                }
            }
            // Retry only the explicitly retired, never-published preparation
            // path. Failure after publication/execution retains explicit recovery
            // semantics. Strictly decreasing checkpoints bound retries.
            if(witness.phase==SeekPhase::Preparing && witness.automatic_selection && !state.cancel
                && !restore.pending && !restore.commit_decided && !restore.ownership_phase
                && historical_restore_->preparation_retiring && !historical_restore_->preparing && !historical_restore_->execution
                && !checkpoint_restoring_ && EarlierSeekCheckpoint(witness.checkpoint,witness.target)) {
                if(!RestoreOperation(RestoreOperationAction::Cancel,nullptr,&restore)) {fail(FailureCode::UndoFailed);return;}
                witness.last_preparation_failure=restore.failure==FailureCode::None?FailureCode::RestorePreflightFailed:restore.failure;
                witness.phase=SeekPhase::RetryingPreparation;witness.pending=true;
                return;
            }
            fail(restore.failure==FailureCode::None?FailureCode::RestoreVerificationFailed:restore.failure);return;
        }
        if(witness.phase==SeekPhase::Recovering) {
            if(restore.phase!=RestoreOperationPhase::Recovered || restore.pending) return;
            witness.recovered_execution_ticks=restore.executed_ticks;
            witness.recovered_execution_intervals=restore.executed_intervals;
            if(!RestoreOperation(RestoreOperationAction::Release,nullptr,&restore)) return;
            witness.phase=SeekPhase::Cancelled;witness.pending=false;
            witness.original_recovered=restore.original_recovered && simulation_->continuation().tick==witness.undo_tick;return;
        }
        if(witness.phase==SeekPhase::Preparing || witness.phase==SeekPhase::Publishing) {
            if(restore.phase==RestoreOperationPhase::Prepared || restore.phase==RestoreOperationPhase::Publishing)
                witness.phase=SeekPhase::Publishing;
            if(restore.phase==RestoreOperationPhase::Prepared && !surface_event_ && !particle_command_pending_.load()
                && particle_copy_ && restore.particle_birth==particle_copy_->witness().birth_prepared) {
                rolling_.telemetry.Mark(ReplayRollingTelemetry::Phase::Publication,ReplayRollingTelemetry::Now());
                if(!RestoreOperation(RestoreOperationAction::Publish,nullptr,&restore)) {
                    state.cancel=true;witness.phase=SeekPhase::Recovering;
                    witness.failure=restore.failure==FailureCode::None?FailureCode::RestoreVerificationFailed:restore.failure;
                }
                return;
            }
            if(restore.phase==RestoreOperationPhase::Held && !restore.pending) witness.phase=SeekPhase::AwaitingCommit;
            return;
        }
        if(witness.phase==SeekPhase::AwaitingCommit) {
            if(surface_event_ || particle_command_pending_.load()) return;
            if(restore.ownership_phase==ReplaySeekOwnership::Phase::ExecutionActive) {
                witness.phase=SeekPhase::Restored;return;
            }
            if(restore.pending) return;
            if(!RestoreOperation(RestoreOperationAction::BeginExecution,nullptr,&restore)) {
                state.cancel=true;witness.phase=SeekPhase::Recovering;
                witness.failure=restore.failure==FailureCode::None?FailureCode::RestoreVerificationFailed:restore.failure;
                return;
            }
            return;
        }
        if(witness.phase==SeekPhase::Held || witness.phase==SeekPhase::Committing) {
            if(restore.ownership_phase==ReplaySeekOwnership::Phase::CSettled && !restore.pending
                && tick_advance_.phase==TickAdvancePhase::CompletionBlocked) {
                witness.pending=false;witness.release_result=SeekReleaseResult::RequiresResume;return;
            }
            if(restore.phase!=RestoreOperationPhase::Committed || restore.pending) return;
            if(!RestoreOperation(RestoreOperationAction::Release,nullptr,&restore)) return;
            witness.phase=SeekPhase::Held;witness.pending=false;return;
        }
    }
    if(DriveRollingReplacement())return;
    if(witness.phase==SeekPhase::Restored) {
        if(surface_event_ || particle_command_pending_.load() || !HistoricalExecutionAdmitted()) return;
        rolling_.telemetry.Mark(ReplayRollingTelemetry::Phase::Resimulation,ReplayRollingTelemetry::Now());
        if(simulation_->continuation().tick==witness.target) {
            tick_advance_={TickAdvancePhase::Held,FailureCode::None,witness.target,witness.target};
            if(!RestoreOperation(RestoreOperationAction::SettleTarget,nullptr,&restore)) {fail(FailureCode::RestoreVerificationFailed);return;}
            witness.phase=SeekPhase::Held;witness.pending=false;return;
        }
        const auto advanced=AdvanceToTick(witness.target,this,&ObserveSeekHold);
        if(advanced.phase==TickAdvancePhase::Failed) {fail(advanced.failure);return;}
        witness.phase=SeekPhase::Advancing;return;
    }
    if(witness.phase==SeekPhase::Advancing) {
        if(state.advance_failure_tick && tick_advance_.phase==TickAdvancePhase::Held
            && simulation_->continuation().tick==*state.advance_failure_tick) {
            TraceSeekAdvanceFailure(*state.advance_failure_tick);
            state.advance_failure_tick.reset();
            tick_advance_.phase=TickAdvancePhase::Failed;tick_advance_.failure=FailureCode::AdvanceFailed;
        }
        if(tick_advance_.phase==TickAdvancePhase::Failed) {fail(tick_advance_.failure);return;}
        if(tick_advance_.phase==TickAdvancePhase::Held && simulation_->continuation().tick==witness.target) {
            if(!RestoreOperation(RestoreOperationAction::SettleTarget,nullptr,&restore)) {fail(FailureCode::RestoreVerificationFailed);return;}
            witness.phase=SeekPhase::Held;witness.pending=false;
        }
    }
}
