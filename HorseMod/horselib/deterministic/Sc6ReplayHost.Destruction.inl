bool Sc6ReplayHost::StopForDestruction() noexcept
{
    return Stop() && active_!=this && !registered_ && !application_hook_
        && !application_active_ && application_phase_==ApplicationPhase::Idle;
}
