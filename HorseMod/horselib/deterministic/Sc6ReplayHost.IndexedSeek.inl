Status Sc6ReplayHost::RequestIndexedSeek(std::uint64_t tick) noexcept
{
    auto* self=active_;
    if(!self || GetCurrentThreadId()!=self->thread_) return Status::failure(FailureCode::WrongThread);
    if(self->SessionExitRequested() || self->depth_ || !self->CheckBinding()) return Status::failure(FailureCode::IllegalTransition);
    // This queue is serviced by AdvanceInteriorHold. Accept only an existing
    // external hold; playback does not drive this queue or implicitly pause.
    // Callers must establish the native hold before requesting an indexed seek.
    if(self->interior_phase_!=InteriorPhase::Holding)
        return Status::failure(FailureCode::IllegalTransition);
    if(self->index_.witness().phase!=ReplayTickIndex::Phase::Complete || tick>=self->index_.witness().entries)
        return Status::failure(FailureCode::InvalidConfiguration);
    self->queued_indexed_seek_=tick;
    self->indexed_seek_failure_=FailureCode::None;
    return Status::success();
}

Status Sc6ReplayHost::RequestIndexedRound(int direction) noexcept
{
    auto* self=active_;
    if(!self || GetCurrentThreadId()!=self->thread_) return Status::failure(FailureCode::WrongThread);
    if(direction!=-1 && direction!=1) return Status::failure(FailureCode::InvalidConfiguration);
    const auto rows=self->index_.entries();
    if(self->index_.witness().phase!=ReplayTickIndex::Phase::Complete || rows.empty())
        return Status::failure(FailureCode::InvalidConfiguration);
    const auto current=(std::min)(self->simulation_->continuation().tick,std::uint64_t(rows.size()-1));
    const auto round=std::int64_t(rows[static_cast<std::size_t>(current)].round)+direction;
    for(const auto& row:rows) if(row.round==round) return RequestIndexedSeek(row.tick);
    return Status::failure(FailureCode::InvalidConfiguration);
}

void Sc6ReplayHost::DriveIndexedSeekIntent() noexcept
{
    const auto index=index_.witness();
    if(SessionExitRequested()) { queued_indexed_seek_.reset(); return; }
    if(!queued_indexed_seek_) return;
    if(index.phase!=ReplayTickIndex::Phase::Complete) {
        queued_indexed_seek_.reset();indexed_seek_failure_=FailureCode::GenerationMismatch;return;
    }
    if(seek_.witness.phase==SeekPhase::Failed) {
        queued_indexed_seek_.reset();indexed_seek_failure_=seek_.witness.failure;return;
    }
    if(interior_phase_==InteriorPhase::Failed) {
        queued_indexed_seek_.reset();indexed_seek_failure_=FailureCode::GenerationMismatch;return;
    }
    if(interior_phase_!=InteriorPhase::Holding || capture_operation_.phase!=CapturePhase::Idle
        || seek_retirement_pending_ || surface_event_ || particle_command_pending_.load()) return;
    const auto phase=seek_.witness.phase;
    if(phase!=SeekPhase::Idle && phase!=SeekPhase::Held && phase!=SeekPhase::Cancelled) return;
    if(seek_.witness.pending) return;
    const auto target=*queued_indexed_seek_;
    if(target==simulation_->continuation().tick) {
        queued_indexed_seek_.reset();
        TraceIndexedSeekAlreadyHeld(target);
        return;
    }
    if(historical_restore_) {
        // Complete C and all application/render tails with B still retained.
        // AdvanceSeek owns the eventual irreversible commit. No new request is
        // published until that operation has actually relinquished its owners.
        if(phase==SeekPhase::Held) {
            SeekWitness witness{};
            if(!SeekOperation(SeekAction::CompleteTarget,nullptr,0,&witness)) {
                // This is still an uncommitted seek with its original B. A
                // replacement coalesces intent; it does not authorize Resume
                // merely to complete an interior interval. Recover that B and
                // retain the latest queued target until recovery/retirement
                // finish. The subsequent Begin owns a fresh transaction.
                if(witness.release_result==SeekReleaseResult::RequiresResume
                    && SeekOperation(SeekAction::Cancel,nullptr,0,&witness)) return;
                queued_indexed_seek_.reset();indexed_seek_failure_=FailureCode::IllegalTransition;
            }
        }
        return;
    }
    if(checkpoint_restoring_) return;
    SeekWitness witness{};
    const bool accepted=SeekOperation(SeekAction::Begin,nullptr,target,&witness);
    queued_indexed_seek_.reset();
    if(!accepted) indexed_seek_failure_=FailureCode::RestorePreflightFailed;
    TraceIndexedSeek(target,accepted,witness.checkpoint);
}

