Sc6ReplayHost::CheckpointHandle Sc6ReplayHost::ResolveOwnedCheckpoint(const Checkpoint* address) const noexcept
{
    if(!address || GetCurrentThreadId()!=thread_) return {};
    for(const auto& weak:retained_checkpoints_) {
        auto candidate=weak.lock();
        if(candidate && candidate.get()==address) return candidate;
    }
    return {};
}

Status Sc6ReplayHost::RetireRejectedIndexCheckpoints() noexcept
{
    const auto phase=index_.witness().phase;
    const bool recording=phase==ReplayTickIndex::Phase::Recording;
    if(!recording && phase!=ReplayTickIndex::Phase::Complete)return Status::success();
    if(seek_retirement_failed_)return Status::failure(FailureCode::PresentationFailed);
    if(recording && seek_retirement_pending_)return Status::success();
    if(!CanRetireSeekCheckpoint() || historical_restore_ || checkpoint_restoring_
        || surface_event_ || particle_command_pending_.load())return Status::failure(FailureCode::IllegalTransition);
    const auto tick=simulation_->continuation().tick;
    bool checked=false;
    for(std::size_t i=0;i<checkpoint_pins_.size();) {
        const auto* image=checkpoint_pins_[i].get();
        if(image==seek_.checkpoint.get() || image->session!=checkpoint_session_) {++i;continue;}
        IndexCheckpointHealth* health{};
        for(auto& row:index_checkpoint_health_)if(auto owner=row.owner.lock();owner && owner.get()==image){health=&row;break;}
        if(!health)for(auto& row:index_checkpoint_health_)if(row.session!=checkpoint_session_ || (row.owner.expired() && row.phase!=IndexCheckpointHealth::Phase::Invalid)) {
            row={};row.owner=checkpoint_pins_[i];row.session=checkpoint_session_;row.captured_tick=image->execution.tick;
            health=&row;break;
        }
        if(!health)return Status::failure(FailureCode::CapacityExceeded);
        if(health->phase!=IndexCheckpointHealth::Phase::Invalid) {
            // At most one native ownership check per completed application
            // during indexing, with30 native ticks between this pin's samples.
            // Actual successful/failing coordinates define the observed bracket.
            if(recording && (checked || tick<health->next_check)){++i;continue;}
            const auto status=image->vfx.ValidateRetainedOwners();checked=true;
            health->last_check=tick;
            health->next_check=tick>UINT64_MAX-30?UINT64_MAX:tick+30;
            health->failure=status.code;
            if(status.ok()) {
                health->phase=IndexCheckpointHealth::Phase::ValidAtLastCheck;
                health->last_valid=tick;
                if(health->valid_samples!=UINT32_MAX)++health->valid_samples;
                if(health->valid_samples==1)TraceIndexCheckpointHealth(*image,*health);
                ++i;continue;
            }
            if(status.code==FailureCode::ContextUnavailable || status.code==FailureCode::WrongThread) {
                health->phase=IndexCheckpointHealth::Phase::Deferred;
                if(health->deferred_samples!=UINT32_MAX)++health->deferred_samples;
                if(health->deferred_samples==1)TraceIndexCheckpointHealth(*image,*health);
                ++i;continue; // No ownership loss established; never retire it.
            }
            health->phase=IndexCheckpointHealth::Phase::Invalid;
            health->first_invalid=tick;
            TraceIndexCheckpointHealth(*image,*health);
            DiagnoseCheckpointOwner(*image); // Existing captured-identity diagnostic.
        }
        // Only our index pin is released. External owners remain accounted;
        // native/GPU retirement still blocks the next application until joined.
        TraceRejectedIndexCheckpoint(*image);
        std::size_t retained{};
        const auto status=OwnCheckpoint(CheckpointOwnership::Release,image,&retained);
        if(!status.ok())return status;
    }
    return Status::success();
}

Status Sc6ReplayHost::OwnCheckpoint(CheckpointOwnership action,const Checkpoint* checkpoint,
    std::size_t* retained_count) noexcept
{
    auto* self=active_;
    if(!self || !retained_count) return Status::failure(FailureCode::ContextUnavailable);
    if(GetCurrentThreadId()!=self->thread_) return Status::failure(FailureCode::WrongThread);
    *retained_count=self->checkpoint_pins_.size();
    if(self->depth_ || !self->CheckBinding() || !self->CanRetireSeekCheckpoint()) {
        if(auto owned=self->ResolveOwnedCheckpoint(checkpoint))self->DiagnoseCheckpointOwner(*owned);
        return Status::failure(FailureCode::IllegalTransition);
    }
    auto& pins=self->checkpoint_pins_;
    if(action==CheckpointOwnership::ReleaseAll) {
        pins.clear();
        if(Sc6ReplayParticleCopy::capture_retirement_pending()) self->seek_retirement_pending_=true;
        *retained_count=0;return Status::success();
    }
    auto existing=std::find_if(pins.begin(),pins.end(),[&](const auto& p){return p.get()==checkpoint;});
    if(action==CheckpointOwnership::Release) {
        if(existing==pins.end()) return Status::failure(FailureCode::IllegalTransition);
        pins.erase(existing);
        if(Sc6ReplayParticleCopy::capture_retirement_pending()) self->seek_retirement_pending_=true;
    } else if(action==CheckpointOwnership::Retain) {
        if(existing!=pins.end()) return Status::success();
        // Resolve through the accounted registry before dereferencing a caller
        // address. This also prevents importing unowned snapshot allocations.
        auto owned=self->ResolveOwnedCheckpoint(checkpoint);
        if(!owned || owned->session!=self->checkpoint_session_
            || owned->boundary!=PauseBoundary::CompletedApplication || owned->task || owned->event
            || !owned->gpu || !owned->surface || !owned->historical_target_shape_supported()) {
            if(owned)self->DiagnoseCheckpointOwner(*owned);
            return Status::failure(FailureCode::GenerationMismatch);
        }
        try {
            if(pins.size()==pins.capacity()) {
                // Include the complete new pointer array while the old array
                // still exists. Checkpoint payloads are already accounted.
                if(pins.size()==pins.max_size() || (pins.size()+1)*sizeof(CheckpointHandle)>self->AdmissionRemaining())
                    return Status::failure(FailureCode::CapacityExceeded);
                pins.reserve(pins.size()+1);
            }
            pins.push_back(std::move(owned));
        } catch(...) {return Status::failure(FailureCode::CapacityExceeded);}
    } else return Status::failure(FailureCode::IllegalTransition);
    *retained_count=pins.size();return Status::success();
}
