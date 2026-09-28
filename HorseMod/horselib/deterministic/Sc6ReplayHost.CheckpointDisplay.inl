// Only the pre-overlay replay display is omitted. Particle GPU snapshots and
// every simulation/application participant retain their existing ownership.
bool Sc6ReplayHost::CaptureCheckpointDisplay(Checkpoint& output) const noexcept
{
    if(corrected_capture_ && capture_driving_ && rolling_.correcting && historical_restore_ && checkpoint_restoring_
        && rolling_.witness.current>=7 && output.session==checkpoint_session_ && output.execution.tick>=rolling_.witness.current-7
        && output.execution.tick<=rolling_.witness.current) {
        output.surface.reset();output.rolling_display_omitted=true;return true;
    }
    if (!held_surface_ || !held_surface_->retained || !held_surface_->image.image
        || held_surface_->session != output.session || held_surface_->tick != output.execution.tick
        || held_surface_->epoch != output.epoch) return false;
    output.rolling_display_omitted = !historical_restore_ && !checkpoint_restoring_
        && rolling_.witness.phase == RollingPhase::Capturing
        && rolling_.witness.current == output.execution.tick;
    output.surface = output.rolling_display_omitted ? nullptr : held_surface_;
    return true;
}

std::shared_ptr<const Sc6ReplayHost::SurfaceSnapshot>
Sc6ReplayHost::RestoreCheckpointDisplay(const Checkpoint& target) const noexcept
{
    if (!target.rolling_display_omitted) {
        const auto& image = target.surface;
        if (image && image->retained && image->image.image && image->session == target.session
            && image->tick == target.execution.tick && image->epoch == target.epoch) return image;
        return {};
    }
    // A missing image is authorized only for this exact ring-owned T-7. Public
    // historical seeking cannot use a rolling-only checkpoint or an arbitrary
    // same-tick replacement. B remains the displayed frame until C completes.
    const auto& w = rolling_.witness;
    if (target.surface || w.phase != RollingPhase::Seeking || w.current < 7
        || target.session != checkpoint_session_ || target.execution.tick != w.current - 7
        || rolling_.checkpoints[target.execution.tick % rolling_.checkpoints.size()].get() != &target
        || !held_surface_ || !held_surface_->retained || !held_surface_->image.image
        || held_surface_->session != target.session || held_surface_->tick != w.current) return {};
    return held_surface_;
}
