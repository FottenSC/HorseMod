// Render-thread ownership handoff, shared with the bounded local fault fixture.
// All fallible checks precede registry publication and metadata-only transfers.

bool Sc6ReplayParticleCopy::CanBeginLightingExecution() const noexcept
{
    return !(lighting_executing_ && !lighting_execution_settled_) && lighting_dirty_ && !lighting_transferred_
        && LightingImageMatches(true) && LightingPrivateUndoMatches();
}

void Sc6ReplayParticleCopy::TransferLightingExecution() noexcept
{
    // Native renderer owns all installed A backing after admission. Its
    // updates may free or replace any address below. B remains detached.
    for(auto& row:lighting_a_.allocations) row.installed=nullptr;
    for(auto& map:lighting_a_.maps) map.installed={};
    lighting_executing_=true;lighting_execution_settled_=false;
}

bool Sc6ReplayParticleCopy::BeginLightingExecution() noexcept
{
    if(!CanBeginLightingExecution())return false;
    TransferLightingExecution();return true;
}

bool Sc6ReplayParticleCopy::CanBeginVisibilityExecution() const noexcept
{
    if(!view_state_) return true;
    return !(visibility_executing_ && !visibility_execution_settled_) && visibility_dirty_ && visibility_storage_published_ && material_dirty_
        && visibility_native_a_refs_==visibility_a_.queries.size() && VisibilityMatches(visibility_a_);
}

void Sc6ReplayParticleCopy::TransferVisibilityExecution() noexcept
{
    if(!view_state_)return;
    // A's native references now belong to the map which native rendering
    // updates. B's displaced native references and separate leases remain.
    visibility_installed_={};visibility_native_a_refs_=0;
    visibility_executing_=true;visibility_execution_settled_=false;
}

bool Sc6ReplayParticleCopy::BeginVisibilityExecution() noexcept
{
    if(!CanBeginVisibilityExecution())return false;
    TransferVisibilityExecution();return true;
}

bool Sc6ReplayParticleCopy::ContinueExecutionForUndo() noexcept
{
    if(!execution_started_ || !execution_settled_ || !completion_.retired()
        || execution_settlement_!=RestoreSettlement::CommitCurrent
        || !witness_.native_write_uncommitted || !witness_.execution_work_complete
        || !Bindings(reinterpret_cast<void*>(world_),false)
        || !SelectionMatches(execution_selection_) || !BirthOwnersBinding()) return false;
    // Preflight every fallible image check before relinquishing any owner.
    // Registry continuation itself validates before its first metadata write.
    // The remaining transfers only clear pointer/flag metadata: no native
    // calls, releases, allocation, or further rejection after registry handoff.
    // A rejected request therefore leaves the entire settled chain retryable.
    if(!CanBeginLightingExecution() || !CanBeginVisibilityExecution()
        || !birth_registry_storage_.ContinueExecution()) return false;
    TransferLightingExecution();TransferVisibilityExecution();
    execution_settled_=false;
    execution_settlement_=RestoreSettlement::RecoverOriginal;
    witness_.execution_work_complete=false;
    return true;
}
