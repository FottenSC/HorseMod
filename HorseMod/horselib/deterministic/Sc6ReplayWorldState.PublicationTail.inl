// The host's zero-traversal completion/recovery paths call this. Publication can
// enqueue component work before execution is handed off. A GPU fence cannot
// complete that UWorld work: finish its native consumer before capturing C.
Status Sc6ReplayWorldState::PreparedRestore::CompleteUnexecutedPublicationRenderWork() noexcept
{
    render_diagnostic_={};
    if(!render_executing_ || render_execution_settled_ || !render_published_ || render_undo_started_
        || (render_publication_work_!=PublicationWork::HandedOff && render_publication_work_!=PublicationWork::Completed))
        return Status::failure(FailureCode::IllegalTransition);
    __try {
        const auto epoch=ReadAt<std::uint64_t>(reinterpret_cast<void*>(base_),0x4197170);
        auto status=ValidateBinding(epoch);if(!status.ok())return status;
        if(!CreationRenderFunctionsMatch(base_))return Status::failure(FailureCode::GenerationMismatch);
        std::array<RenderSet,2> live{};
        auto* lock=reinterpret_cast<CRITICAL_SECTION*>(static_cast<std::byte*>(world_)+0x270);
        if(!TryEnterCriticalSection(lock))return Status::failure(FailureCode::RestorePreflightFailed);
        int pending{};
        __try {
            std::memcpy(live.data(),static_cast<std::byte*>(world_)+0x1d0,0xa0);
            for(unsigned i=0;i<2;++i) {
                std::uint64_t hash{};
                if(!FingerprintRenderSet(render_previous_[i],hash) || hash!=render_previous_fingerprint_[i])
                    return Status::failure(FailureCode::RestoreVerificationFailed);
                if(!FingerprintRenderSet(live[i],hash))return Status::failure(FailureCode::GenerationMismatch);
                pending+=ReadAt<int>(live[i].data(),8)-ReadAt<int>(live[i].data(),0x34);
                // Compare the live header before following its live allocations.
                // The saved handoff addresses themselves are never dereferenced.
                if(render_publication_work_==PublicationWork::HandedOff
                    && (live[i]!=render_publication_backing_[i] || hash!=render_publication_fingerprint_[i])) {
                    render_diagnostic_={"unexecuted_publication_queue_changed",i,hash,render_publication_fingerprint_[i]};
                    return Status::failure(FailureCode::GenerationMismatch);
                }
            }
            if(render_publication_work_==PublicationWork::Completed)
                return pending?Status::failure(FailureCode::GenerationMismatch):Status::success();
            status=ValidateRenderWorkStorage(epoch,render_publication_backing_,false);
            if(!status.ok())return status;
            // 141EFEBA0 uses this native weak scratch array. Keep its existing
            // allocation and reject insufficient capacity rather than growing
            // an unaccounted process-global allocation during recovery.
            if(ReadAt<unsigned char>(world_,0x890) || ReadAt<int>(reinterpret_cast<void*>(base_+0x439ba00),8)
                || ReadAt<int>(reinterpret_cast<void*>(base_+0x439ba00),12)<pending)
                return Status::failure(FailureCode::CapacityExceeded);
        } __finally {LeaveCriticalSection(lock);}
        render_publication_work_=PublicationWork::Completing;
        // The native routine joins its component jobs and clears both sets.
        // It runs no world/gameplay tick. Render commands are NOT yet complete:
        // the host still submits the existing ordered render/GPU drain afterward.
        InvokeNativePublicationRenderTail();
        if(ReadAt<unsigned char>(world_,0x890) || ReadAt<int>(reinterpret_cast<void*>(base_+0x439ba00),8))
            return Status::failure(FailureCode::RestoreVerificationFailed);
        for(unsigned i=0;i<2;++i) {
            RenderSet current{};
            std::memcpy(current.data(),static_cast<std::byte*>(world_)+0x1d0+i*0x50,0x50);
            std::uint64_t hash{};
            if(!FingerprintRenderSet(current,hash) || ReadAt<int>(current.data(),8)!=ReadAt<int>(current.data(),0x34)
                || !FingerprintRenderSet(render_previous_[i],hash) || hash!=render_previous_fingerprint_[i])
                return Status::failure(FailureCode::RestoreVerificationFailed);
        }
        status=ValidateBinding(epoch);if(!status.ok())return status;
        render_publication_work_=PublicationWork::Completed;
        render_diagnostic_={"publication_tail_completed",0,static_cast<std::uint64_t>(pending),0};
        render_publication_backing_={};render_publication_fingerprint_={};
        return Status::success();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // Completing remains non-retryable: a fault is not cancellation of
        // native jobs. The owning replay transaction retains B and resources.
        return Status::failure(FailureCode::ContextUnavailable);
    }
}
