    std::uint64_t rolling_last_admission_{};
    bool BeginRollingCampaign()
    {
        const auto rolling=ResolveHorseModExport<bool (*)(ReplayHost::RollingAction,const ReplayHost::Checkpoint*,std::uint64_t,ReplayHost::RollingWitness*)>("horsemod_replay_rolling_operation");
        const auto monitor=ResolveHorseModExport<bool (*)(void*,ReplayHost::PauseMonitor)>("horsemod_set_replay_pause_monitor");
        ReplayHost::RollingWitness state{};
        if(!rolling || !monitor || !monitor(this,[](void* context,const ReplayHost::InteriorWitness& held) {
            static_cast<ReplayQualificationMod*>(context)->ObserveRollingCampaign(held);
        })) {
            Fail("rolling_begin_rejected");return false;
        }
        bool began{};
        if(request_.rolling_corrections.empty()) {
            began=rolling(ReplayHost::RollingAction::Begin,combat_checkpoint_.get(),request_.rolling_cycles,&state);
        } else if(combat_checkpoint_) {
            using Correction=Horse::Deterministic::ReplayCorrectionRequest;
            const auto scheduled=ResolveHorseModExport<bool (*)(std::uint32_t,const ReplayHost::Checkpoint*,std::uint64_t,const Correction*,std::size_t,ReplayHost::RollingWitness*)>("horsemod_begin_scheduled_rolling");
            std::vector<Horse::Deterministic::ReplayInputOverride> edits;
            std::vector<Correction> rows;
            const auto grouped=GroupRollingCorrectionRequests(request_.rolling_corrections,
                combat_checkpoint_->session,static_cast<std::uint64_t>(combat_checkpoint_->source.round)+1,
                edits,rows);
            began=grouped && scheduled && scheduled(ReplayHost::rolling_schedule_protocol,combat_checkpoint_.get(),
                request_.rolling_cycles,rows.data(),rows.size(),&state);
        }
        if(!began) {Fail("rolling_begin_rejected");return false;}
        combat_checkpoint_.reset(); // The host's window now owns the capture.
        return false;
    }

    void ObserveRollingCampaign(const ReplayHost::InteriorWitness& held)
    {
        const auto rolling=ResolveHorseModExport<bool (*)(ReplayHost::RollingAction,const ReplayHost::Checkpoint*,std::uint64_t,ReplayHost::RollingWitness*)>("horsemod_replay_rolling_operation");
        ReplayHost::RollingWitness state{};
        if(!rolling || !rolling(ReplayHost::RollingAction::Read,nullptr,0,&state)) {Fail("rolling_read_failed");return;}
        if(state.phase==ReplayHost::RollingPhase::Failed) {Fail("rolling_native_failure");return;}
        if(state.phase==ReplayHost::RollingPhase::Seeking) {
            const auto seek=ResolveHorseModExport<bool (*)(ReplayHost::SeekAction,const ReplayHost::Checkpoint*,std::uint64_t,ReplayHost::SeekWitness*,void*,ReplayHost::SeekObserver)>("horsemod_replay_seek_operation");
            ReplayHost::SeekWitness native{};
            if(!seek || !seek(ReplayHost::SeekAction::Read,nullptr,0,&native,nullptr,nullptr)) {Fail("rolling_seek_read_failed");return;}
            if(native.phase==ReplayHost::SeekPhase::Restored && rolling_last_admission_!=native.origin) {
                if(native.origin!=request_.historical_anchor_tick+7+state.cycles || native.checkpoint+7!=native.origin || !native.pending) {Fail("rolling_admission_depth");return;}
                rolling_last_admission_=native.origin;
                Output::send<LogLevel::Default>(STR("[ReplayQualification] historical combat execution admitted run_id={} from_tick={} to_tick={} B_retained=true\n"),
                    RC::to_generic_string(request_.run_id),native.origin,native.checkpoint);
            }
        }
        if(state.phase!=ReplayHost::RollingPhase::Complete || held.surface_pending)return;
        if(state.cycles!=request_.rolling_cycles || state.resimulated_ticks!=7*state.cycles
            || held.tick!=request_.historical_anchor_tick+6+state.cycles || held.boundary!=ReplayHost::PauseBoundary::CompletedApplication) {
            Fail("rolling_completion_invalid");return;
        }
        historical_execution_started_=true;historical_rewind_ticks_=state.resimulated_ticks;
        historical_rewind_intervals_=state.resimulated_intervals;
        Output::send<LogLevel::Default>(STR("[ReplayQualification] rolling completed run_id={} first={} last={} cycles={} resimulated_ticks={} resimulated_intervals={} peak_bytes={} missed=0\n"),
            RC::to_generic_string(request_.run_id),request_.historical_anchor_tick,held.tick,state.cycles,state.resimulated_ticks,state.resimulated_intervals,state.peak_bytes);
        const auto monitor=ResolveHorseModExport<bool (*)(void*,ReplayHost::PauseMonitor)>("horsemod_set_replay_pause_monitor");
        const auto resume=ResolveHorseModExport<std::uint16_t (*)()>("horsemod_resume_replay_execution");
        if(request_.ground_motion_perturb) {
            using Witness=Horse::Deterministic::Sc6ReplayGroundDebrisState::MotionProbeWitness;
            const auto probe=ResolveHorseModExport<bool (*)(std::uint32_t,std::uint64_t,Witness*)>("horsemod_probe_ground_motion");
            Witness changed{};
            if(!probe || !probe(1,held.tick,&changed) || !changed.roots || !changed.meshes || changed.changed!=changed.meshes) {
                Output::send<LogLevel::Warning>(STR("[ReplayQualification] ground motion probe rejected run_id={} check={} tick={} roots={} meshes={} changed={}\n"),
                    RC::to_generic_string(request_.run_id),RC::to_generic_string(changed.check),held.tick,changed.roots,changed.meshes,changed.changed);
                Fail("ground_motion_probe_failed");return;
            }
            Output::send<LogLevel::Default>(STR("[ReplayQualification] ground motion perturbed run_id={} tick={} roots={} meshes={} changed={} owned_bytes={} native_move=true logical_unchanged=true rng_unchanged=true before_resume=true\n"),
                RC::to_generic_string(request_.run_id),held.tick,changed.roots,changed.meshes,changed.changed,changed.owned_bytes);
        }
        if(!rolling(ReplayHost::RollingAction::Release,nullptr,0,&state) || !monitor || !monitor(nullptr,nullptr)) {Fail("rolling_release_failed");return;}
        interior_completed_=true;interior_started_=false;historical_operation_started_=false;
        world_pause_completed_=true;world_resume_frame_=held.tick;world_resume_measured_=false;
        world_resume_max_gap_us_=world_resume_late_gaps_=0;
        world_resume_viewport_=viewport_frame_count_.load(std::memory_order_relaxed);
        world_resume_started_=world_resume_previous_=std::chrono::steady_clock::now();
        if(!resume || resume()) {Fail("rolling_resume_failed");return;}
    }
