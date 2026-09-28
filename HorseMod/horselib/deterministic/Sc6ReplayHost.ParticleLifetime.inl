bool Sc6ReplayHost::ProtectParticleLifecycle(void* context,void* component,bool completion) noexcept
{
    auto* self=static_cast<Sc6ReplayHost*>(context);
    if(!self || active_!=self || !self->historical_restore_ || !self->historical_restore_->execution)return false;
    auto& transaction=*self->historical_restore_;auto& execution=*transaction.execution;
    if(!execution.ownership.retains_undo() || !transaction.undo.vfx.RetainsComponent(component))return false;
    // Complete B owns this exact UObject/serial. Never let a speculative native
    // completion callback or destruction retire it. The C attempt becomes
    // uncommittable and is quiesced at the next complete application boundary.
    // New C-only objects and irreversible B retirement take the native route.
    void* empty{};
    execution.protected_particle.compare_exchange_strong(empty,component);
    execution.protected_particle_routes.fetch_or((completion?1u:2u)
        | (execution.ownership.phase()==ReplaySeekOwnership::Phase::Recovering?4u:0u));
    return true;
}

bool Sc6ReplayHost::HistoricalParticleAbortPending() const noexcept
{
    if(!historical_restore_ || !historical_restore_->execution)return false;
    const auto& execution=*historical_restore_->execution;
    const auto phase=execution.ownership.phase();
    return execution.protected_particle.load() && execution.ownership.retains_undo()
        && phase!=ReplaySeekOwnership::Phase::RecoveryQuiescing && phase!=ReplaySeekOwnership::Phase::Recovering
        && phase!=ReplaySeekOwnership::Phase::FailedRecoverable;
}

