    enum class NativeExitPhase {Idle,Waiting,Complete};
    NativeExitPhase native_exit_phase_{};
    std::array<std::uint64_t,12> native_exit_executor_{};
    std::array<std::uint64_t,15> native_exit_world_{};
    std::chrono::steady_clock::time_point native_exit_started_;
    std::uint64_t native_exit_viewport_{};

    void BeginNativeSessionExit()
    {
        using Index=Horse::Deterministic::ReplayTickIndex;
        const auto read=ResolveHorseModExport<bool (*)(std::uint64_t*,std::size_t)>("horsemod_get_replay_executor_status");
        const auto world=ResolveHorseModExport<bool (*)(std::uint64_t*,std::size_t)>("horsemod_get_replay_host_status");
        const auto index=ResolveHorseModExport<bool (*)(ReplayHost::IndexAction,Index::Witness*,std::size_t)>("horsemod_replay_index_operation");
        Index::Witness state{};
        const bool recovered_endpoint=indexed_recovery_==IndexedRecovery::Exiting;
        if(!index_host_completion_seen_ || index_capture_!=IndexCapture::Resuming || !read || !world || !index
            || !read(native_exit_executor_.data(),native_exit_executor_.size())
            || !world(native_exit_world_.data(),native_exit_world_.size())
            || !index(ReplayHost::IndexAction::Read,&state,0)
            || (recovered_endpoint?(state.phase!=Index::Phase::Complete || state.entries!=indexed_recovery_B_.frame+1
                || state.end_policy!=Index::EndPolicy::RetainedSourceStop || !state.unsupported_native_tail)
                :(state.phase!=Index::Phase::Recording || state.entries!=trajectory_last_.frame+1))
            || !boundary_started_ || boundary_failed_ || !boundary_observer_.healthy())
        {Fail("native_exit_observation_boundary_failed");return;}
        native_exit_started_=std::chrono::steady_clock::now();
        native_exit_viewport_=viewport_frame_count_.load(std::memory_order_relaxed);
        if(!boundary_observer_.Stop() || !presentation_observer_.Stop())
        {Fail("native_exit_observer_detach_failed");return;}
        boundary_started_=false;
        native_exit_phase_=NativeExitPhase::Waiting;
        Output::send<LogLevel::Default>(STR("[ReplayQualification] native session exit requested run_id={} observed_prefix_tick={} index_entries={} checkpoint170_retained=true cleanup_owner=host\n"),
            RC::to_generic_string(request_.run_id),trajectory_last_.frame,state.entries);
        if(request_.probe_interactive_controls && recovered_endpoint) {
            Output::send<LogLevel::Default>(STR("[ReplayQualification] native menu exit waiting run_id={} B={} no_transaction=true observer_request=false\n"),
                RC::to_generic_string(request_.run_id),indexed_recovery_B_.frame);
            return; // The visible production control must initiate teardown.
        }
        if(!navigator_.RequestReplayExit()) Fail("native_exit_request_failed");
    }

    void PollNativeSessionExit()
    {
        if(std::chrono::steady_clock::now()-native_exit_started_>std::chrono::seconds(20))
        {Fail("native_session_exit_timeout");return;}
        std::string detail;
        const auto state=navigator_.Tick(false,true,detail);
        if(state==Horse::Qualification::NavigationState::Failed) {Fail("native_session_exit_navigation_failed");return;}
        if(state!=Horse::Qualification::NavigationState::ReplayListReady) return;
        const auto read=ResolveHorseModExport<bool (*)(std::uint64_t*,std::size_t)>("horsemod_get_replay_executor_status");
        const auto host=ResolveHorseModExport<bool (*)(ReplayHost::IndexCheckpointWitness*)>("horsemod_read_index_checkpoint");
        std::array<std::uint64_t,6> status{};ReplayHost::IndexCheckpointWitness checkpoint{};
        if(!battle_terminate_observed_ || !read || !host || !read(status.data(),status.size()) || status[0] || status[5]
            || host(&checkpoint)) {Fail("native_session_exit_cleanup_incomplete");return;}
        native_exit_phase_=NativeExitPhase::Complete;
        Output::send<LogLevel::Default>(STR("[ReplayQualification] native session exit completed run_id={} observed_prefix_tick={} elapsed_us={} native_terminate_observed=true scene=replay_list executor_disabled=true host_inactive=true\n"),
            RC::to_generic_string(request_.run_id),trajectory_last_.frame,
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-native_exit_started_).count());
        if(indexed_recovery_==IndexedRecovery::Exiting) {
            // B was witnessed before native teardown. Never read destroyed
            // HUD/battle objects or count cleanup as authored continuation.
            index_publication_=IndexPublication::Complete;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] index released run_id={} bytes=0 hooks_removed=true\n"),RC::to_generic_string(request_.run_id));
            Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed recovery complete run_id={} B={} native_tick={} callbacks={} HUD_unchanged=true authored_continuation_unavailable=true resume_requested=false cleanup=native_exit B_observed_before_native_exit=true\n"),
                RC::to_generic_string(request_.run_id),indexed_recovery_B_.frame,indexed_recovery_B_.frame,boundary_samples_);
            indexed_recovery_=IndexedRecovery::Done;
        }
        CompleteTrajectory();
    }
