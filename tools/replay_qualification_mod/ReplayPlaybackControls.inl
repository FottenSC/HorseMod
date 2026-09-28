// Read-only monitor for the actual production playback pause. Windows input
// drives the controls; observations only reject divergence and missing proof.
struct PlaybackControlsProof {
    bool started{},completed{};
    ReplayTrajectorySample sample{};
    std::uint64_t callbacks{},epoch{},resume{},frames{};
    std::chrono::steady_clock::time_point started_at{},resumed_at{};
} playback_controls_;

void ObservePlaybackControls(const ReplayHost::InteriorWitness& held) {
    if(state_==State::Failed || playback_controls_.completed || held.tick<=request_.index_seek_target)return;
    if(held.phase!=ReplayHost::InteriorPhase::Holding)return;
    auto& proof=playback_controls_;
    if(!proof.started && held.surface_pending)return;
    ReplayTrajectorySample sample{};
    if(held.boundary!=ReplayHost::PauseBoundary::CompletedApplication || !held.application_idle
        || !held.engine_idle || !held.world_idle || !held.arena_empty || held.pending_task
        || held.tick+120>request_.index_seek_target+request_.index_seek_continuation
        || !ReadReplayTrajectory(battle_manager_,sample,true)) {Fail("playback_control_boundary_invalid");return;}
    if(!proof.started) {
        proof.started=true;proof.sample=sample;proof.callbacks=boundary_samples_;proof.epoch=held.epoch;
        proof.resume=held.ui_resume_requests;proof.frames=held.surface_frames;proof.started_at=std::chrono::steady_clock::now();
        Output::send<LogLevel::Default>(STR("[ReplayQualification] playback controls held run_id={} tick={} resume_baseline={}\n"),
            RC::to_generic_string(request_.run_id),held.tick,proof.resume);
    }
    if(sample!=proof.sample || boundary_samples_!=proof.callbacks || held.epoch!=proof.epoch)
        {Fail("playback_control_hold_advanced");return;}
    if(held.ui_resume_requests==proof.resume)return;
    const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-proof.started_at).count();
    if(held.ui_resume_requests!=proof.resume+1 || elapsed<500000 || held.surface_frames-proof.frames<30)
        {Fail("playback_control_hold_coverage_missing");return;}
    proof.completed=true;
    proof.resumed_at=std::chrono::steady_clock::now();
    Output::send<LogLevel::Default>(STR("[ReplayQualification] playback controls resumed run_id={} tick={} held_us={} frames={} unchanged=true application_complete=true\n"),
        RC::to_generic_string(request_.run_id),held.tick,elapsed,held.surface_frames-proof.frames);
}
