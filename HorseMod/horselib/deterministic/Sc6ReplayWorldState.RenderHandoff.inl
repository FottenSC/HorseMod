Status Sc6ReplayWorldState::PreparedRestore::BeginRenderWorkExecution(std::size_t retirement_budget) noexcept
{
    if(render_executing_ || retirement_budget>SIZE_MAX-owned_bytes_) return Status::failure(FailureCode::IllegalTransition);
    auto status=ValidateRenderWork();if(!status.ok()) return status;
    for(unsigned i=0;i<2;++i) {
        std::uint64_t hash{};
        if(!FingerprintRenderSet(render_previous_[i],hash) || hash!=render_previous_fingerprint_[i])
            return Status::failure(FailureCode::GenerationMismatch);
    }
    // render_owned_ describes the allocations, not their current sparse-set
    // counts. Capture the actual queue under its native mutation lock.
    __try {
        auto* lock=reinterpret_cast<CRITICAL_SECTION*>(static_cast<std::byte*>(world_)+0x270);
        if(!TryEnterCriticalSection(lock))return Status::failure(FailureCode::RestorePreflightFailed);
        __try {
            status=ValidateRenderWork();if(!status.ok())return status;
            std::memcpy(render_publication_backing_.data(),static_cast<std::byte*>(world_)+0x1d0,0xa0);
            for(unsigned i=0;i<2;++i)
                if(!FingerprintRenderSet(render_publication_backing_[i],render_publication_fingerprint_[i]))
                    return Status::failure(FailureCode::GenerationMismatch);
        } __finally {LeaveCriticalSection(lock);}
    } __except(EXCEPTION_EXECUTE_HANDLER) {return Status::failure(FailureCode::ContextUnavailable);}
    render_publication_work_=PublicationWork::HandedOff;
    render_owned_={}; // Native end-frame execution now owns these addresses.
    render_execution_budget_=retirement_budget;owned_bytes_+=retirement_budget;
    render_executing_=true;
    return Status::success();
}

