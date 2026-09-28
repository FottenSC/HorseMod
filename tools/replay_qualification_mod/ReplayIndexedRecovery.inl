    // Test-only observation and protocol. No captured value is installed by this driver.
    struct IndexedHudWitness {
        std::array<int,4> identity{};
        std::array<unsigned,7> values{};
        unsigned requested{},visibility{};
        std::array<int,2> announcement_identity{};
        unsigned announcement_visibility{},announcement_active{},announcement_enabled{};
        std::array<unsigned,4> announcement_clock{};
        std::array<std::array<std::uint64_t,18>,4> announcement_players{};
        bool operator==(const IndexedHudWitness&) const = default;
    };
    enum class IndexedRecovery { Idle, Armed, Cancelling, HoldingB, Releasing, Exiting, Done };
    IndexedRecovery indexed_recovery_{};
    ReplayTrajectorySample indexed_recovery_B_{};
    IndexedHudWitness indexed_recovery_hud_B_{};
    std::uint64_t indexed_recovery_callbacks_{},indexed_recovery_epoch_{},indexed_recovery_frames_{};
    std::chrono::steady_clock::time_point indexed_recovery_started_{};

    bool ReadIndexedHud(IndexedHudWitness& output) {
        std::vector<RC::Unreal::UObject*> actors;
        RC::Unreal::UObjectGlobals::FindAllOf(L"LuxBattleCockpit",actors);
        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        unsigned matches=0;
        for(auto* actor:actors) {
            const auto* item=RC::Unreal::FUObjectArray::IndexToObject(actor->GetInternalIndex());
            if(!item || item->GetUObject()!=actor || !item->IsValid(false)) continue;
            const auto address=reinterpret_cast<std::uintptr_t>(actor);
            if(*reinterpret_cast<void**>(address+0x98)!=battle_manager_) continue;
            if(++matches!=1 || *reinterpret_cast<std::uintptr_t*>(address)!=base+0x327e1e8) return false;
            auto** binding=actor->GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>(L"CockpitInstance");
            if(!binding || reinterpret_cast<std::uintptr_t>(binding)!=address+0x398 || !*binding) return false;
            auto* widget=*binding;
            const auto* widget_item=RC::Unreal::FUObjectArray::IndexToObject(widget->GetInternalIndex());
            if(!widget_item || widget_item->GetUObject()!=widget || !widget_item->IsValid(false)) return false;
            const auto* visibility=widget->GetValuePtrByPropertyNameInChain<unsigned char>(L"Visibility");
            if(!visibility || reinterpret_cast<std::uintptr_t>(visibility)!=reinterpret_cast<std::uintptr_t>(widget)+0x91) return false;
            output.identity={actor->GetInternalIndex(),item->GetSerialNumber(),widget->GetInternalIndex(),widget_item->GetSerialNumber()};
            output.requested=*reinterpret_cast<unsigned char*>(address+0x388);output.visibility=*visibility;
            if(output.requested>1 || output.visibility>4) return false;
            constexpr const wchar_t* names[]{L"PrevP1VitalPer",L"PrevP2VitalPer",L"PrevComboP1VitalPer",L"PrevComboP2VitalPer",L"StartingTimer",L"DmgEffCount",L"TypeEffCount"};
            for(unsigned i=0;i<output.values.size();++i) {
                const auto* value=widget->GetValuePtrByPropertyNameInChain<unsigned>(names[i]);
                if(!value) return false;
                output.values[i]=*value;
            }
        }
        if(matches!=1)return false;
        actors.clear();RC::Unreal::UObjectGlobals::FindAllOf(L"LuxBattleAnnounce",actors);
        RC::Unreal::UObject* announce_class{};
        for(auto* actor:actors) {
            const auto* item=RC::Unreal::FUObjectArray::IndexToObject(actor->GetInternalIndex());
            if(!item || item->GetUObject()!=actor || !item->IsValid(false))continue;
            const auto address=reinterpret_cast<std::uintptr_t>(actor);
            if(*reinterpret_cast<void**>(address+0x98)!=battle_manager_)continue;
            if(announce_class || *reinterpret_cast<std::uintptr_t*>(address)!=base+0x327db88)return false;
            announce_class=*reinterpret_cast<RC::Unreal::UObject**>(address+0x3e0);
            output.announcement_enabled=*reinterpret_cast<unsigned char*>(address+0x388);
        }
        if(!announce_class || output.announcement_enabled>1)return false;
        actors.clear();RC::Unreal::UObjectGlobals::FindAllOf(L"BA_YouWin_C",actors);matches=0;
        const auto* world=reinterpret_cast<RC::Unreal::UObject*>(battle_manager_)->GetWorld();
        for(auto* widget:actors) {
            const auto* item=RC::Unreal::FUObjectArray::IndexToObject(widget->GetInternalIndex());
            if(!item || item->GetUObject()!=widget || !item->IsValid(false) || widget->GetClassPrivate()!=announce_class
                || widget->GetWorld()!=world)continue;
            const auto address=reinterpret_cast<std::uintptr_t>(widget);
            const auto control=*reinterpret_cast<std::uintptr_t*>(address+0x210);
            if(!*reinterpret_cast<void**>(address+0x208) || !control || *reinterpret_cast<int*>(control+8)<1)continue;
            if(++matches!=1)return false;
            struct Players {RC::Unreal::UObject** data;int count,capacity;};
            const auto* players=widget->GetValuePtrByPropertyNameInChain<Players>(L"ActiveSequencePlayers");
            const auto* stopped=widget->GetValuePtrByPropertyNameInChain<Players>(L"StoppedSequencePlayers");
            const auto* visibility=widget->GetValuePtrByPropertyNameInChain<unsigned char>(L"Visibility");
            if(!players || !stopped || !visibility || players->count<0 || players->count>4
                || players->capacity<players->count || (players->count && !players->data) || stopped->count || *visibility>4)return false;
            output.announcement_identity={widget->GetInternalIndex(),item->GetSerialNumber()};
            output.announcement_visibility=*visibility;output.announcement_active=players->count;
            std::memcpy(output.announcement_clock.data(),reinterpret_cast<void*>(address+0x230),16);
            for(int j=0;j<players->count;++j) {
                auto* player=players->data[j];
                const auto* player_item=player?RC::Unreal::FUObjectArray::IndexToObject(player->GetInternalIndex()):nullptr;
                if(!player_item || player_item->GetUObject()!=player || !player_item->IsValid(false))return false;
                const auto p=reinterpret_cast<std::uintptr_t>(player);
                if(*reinterpret_cast<std::uintptr_t*>(p)!=base+0x373b998)return false;
                auto& row=output.announcement_players[j];row[0]=p;row[1]=player->GetInternalIndex();row[2]=*reinterpret_cast<std::uintptr_t*>(p+0x370);
                unsigned n=3;
                for(auto offset:{0x6a0,0x6a8,0x6b0,0x6c0,0x6d0})std::memcpy(&row[n++],reinterpret_cast<void*>(p+offset),8);
                for(auto offset:{0x6d8,0x750,0x754,0x758,0x75c})row[n++]=*reinterpret_cast<unsigned*>(p+offset);
                for(auto offset:{0x6b8,0x6c8,0x760})row[n++]=*reinterpret_cast<unsigned char*>(p+offset);
                row[n++]=*reinterpret_cast<unsigned*>(p+0x770);row[n++]=*reinterpret_cast<unsigned char*>(p+0x761)&1;
                if(n!=row.size() || row[16] || row[17])return false;
            }
        }
        return matches==1;
    }
    void LogIndexedHud(const IndexedHudWitness& hud,const wchar_t* point) {
        Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed recovery HUD run_id={} point={} actor={}/{} widget={}/{} requested={} visibility={} values={},{},{},{},{},{},{} read_only=true\n"),
            RC::to_generic_string(request_.run_id),point,hud.identity[0],hud.identity[1],hud.identity[2],hud.identity[3],hud.requested,hud.visibility,
            hud.values[0],hud.values[1],hud.values[2],hud.values[3],hud.values[4],hud.values[5],hud.values[6]);
        std::ostringstream clocks,players;
        for(auto word:hud.announcement_clock)clocks<<word<<',';
        for(unsigned i=0;i<hud.announcement_active;++i)for(auto word:hud.announcement_players[i])players<<word<<',';
        Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed recovery announcement run_id={} point={} widget={}/{} enabled={} visibility={} active={} clock={} players={} native_viewport=true read_only=true\n"),
            RC::to_generic_string(request_.run_id),point,hud.announcement_identity[0],hud.announcement_identity[1],hud.announcement_enabled,
            hud.announcement_visibility,hud.announcement_active,RC::to_generic_string(clocks.str()),RC::to_generic_string(players.str()));
    }
    bool BeginIndexedRecoveryObservation() {
        if(request_.index_recovery.empty()) return true;
        if(!ReadReplayTrajectory(battle_manager_,indexed_recovery_B_,true) || !ReadIndexedHud(indexed_recovery_hud_B_)
            || indexed_recovery_hud_B_.requested!=0 || indexed_recovery_hud_B_.visibility!=1) {
            Fail("indexed_recovery_original_B_unreadable");return false;
        }
        LogTrajectorySample(indexed_recovery_B_,1,L"index_recovery_B_before");
        LogIndexedHud(indexed_recovery_hud_B_,L"B_before");
        return true;
    }
    bool ArmIndexedRecovery(ReplayHost::SeekWitness& seek) {
        if(request_.index_recovery.empty()) return true;
        IndexedHudWitness published{};
        if(!ReadIndexedHud(published) || published.identity!=indexed_recovery_hud_B_.identity
            || published.requested!=1 || published.visibility!=3 || seek.commit_decided) {
            Fail("indexed_recovery_A_publication_unproven");return false;
        }
        auto private_B=indexed_recovery_hud_B_;
        private_B.requested=published.requested;private_B.visibility=published.visibility;private_B.values=published.values;
        private_B.announcement_visibility=1;
        // The observed B player/clock is private, not replaced by an expected
        // snapshot. Only A's independently observed Cockpit fields may differ.
        if(published!=private_B) {Fail("indexed_recovery_announcement_B_changed_during_publication");return false;}
        LogIndexedHud(published,L"A_published");
        const auto operation=ResolveHorseModExport<bool (*)(ReplayHost::SeekAction,const ReplayHost::Checkpoint*,std::uint64_t,
            ReplayHost::SeekWitness*,void*,ReplayHost::SeekObserver)>("horsemod_replay_seek_operation");
        const bool published_cancel=request_.index_recovery=="published";
        const bool interior=request_.index_recovery=="interior";
        if(!operation || (!interior && !operation(published_cancel?ReplayHost::SeekAction::Cancel:ReplayHost::SeekAction::InjectSettlementFailure,
            nullptr,0,&seek,nullptr,nullptr))) {Fail("indexed_recovery_arm_rejected");return false;}
        indexed_recovery_=published_cancel?IndexedRecovery::Cancelling:IndexedRecovery::Armed;
        Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed recovery armed run_id={} point={} B={} A={} target={} complete_B=true commit_decided=false\n"),
            RC::to_generic_string(request_.run_id),RC::to_generic_string(request_.index_recovery),indexed_recovery_B_.frame,index_selected_checkpoint_,request_.index_seek_target);
        return !published_cancel;
    }
    // Returns true when this protocol owns the update, including failures.
    bool ObserveIndexedRecovery(const ReplayHost::InteriorWitness& held,ReplayHost::SeekWitness& seek) {
        if(request_.index_recovery.empty() || indexed_recovery_==IndexedRecovery::Idle) return false;
        const auto operation=ResolveHorseModExport<bool (*)(ReplayHost::SeekAction,const ReplayHost::Checkpoint*,std::uint64_t,
            ReplayHost::SeekWitness*,void*,ReplayHost::SeekObserver)>("horsemod_replay_seek_operation");
        if(indexed_recovery_==IndexedRecovery::Armed) {
            if(request_.index_recovery=="interior") {
                if(seek.phase!=ReplayHost::SeekPhase::Held) return false;
                if(held.tick!=208 || seek.commit_decided) {Fail("indexed_interior_wrong_hold");return true;}
                if(!ObserveCompletionRepeat(held)) return true;
                const auto request=ResolveHorseModExport<std::uint16_t (*)(std::uint64_t)>("horsemod_request_indexed_replay_seek");
                if(!request || request(207)!=0 || request(indexed_recovery_B_.frame)!=0) {
                    Fail("indexed_interior_replacement_rejected");return true;
                }
                Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed recovery interior queued run_id={} C=208 B={} latest_request=B pending_repeat=true explicit_resume=false\n"),
                    RC::to_generic_string(request_.run_id),indexed_recovery_B_.frame);
                indexed_recovery_=IndexedRecovery::Cancelling;return true;
            }
            if(seek.phase!=ReplayHost::SeekPhase::Failed) return false;
            const auto read=ResolveHorseModExport<bool (*)(ReplayHost::RestoreOperationAction,const ReplayHost::Checkpoint*,ReplayHost::RestoreOperationWitness*)>("horsemod_replay_restore_operation");
            ReplayHost::RestoreOperationWitness restore{};
            if(!seek.pending || seek.commit_decided || seek.failure!=Horse::Deterministic::FailureCode::AdvanceFailed
                || held.tick!=request_.index_seek_target || held.pending_task || !held.application_idle
                || held.boundary!=ReplayHost::PauseBoundary::CompletedApplication || !read
                || !read(ReplayHost::RestoreOperationAction::Read,nullptr,&restore) || restore.pending
                || restore.phase!=ReplayHost::RestoreOperationPhase::Failed || restore.original_tick!=indexed_recovery_B_.frame
                || restore.ownership_phase!=Horse::Deterministic::ReplaySeekOwnership::Phase::FailedRecoverable
                || !restore.participant || std::string_view(restore.participant)!="injected_render_drained") {
                Fail("indexed_recovery_drained_boundary_unproven");return true;
            }
            Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed recovery drained run_id={} C={} B={} participant=injected_render_drained complete_B=true commit_decided=false\n"),
                RC::to_generic_string(request_.run_id),held.tick,indexed_recovery_B_.frame);
            if(!operation || !operation(ReplayHost::SeekAction::Cancel,nullptr,0,&seek,nullptr,nullptr)) {
                Fail("indexed_recovery_drained_cancel_rejected");return true;
            }
            indexed_recovery_=IndexedRecovery::Cancelling;return true;
        }
        if(indexed_recovery_==IndexedRecovery::Cancelling) {
            if(request_.index_recovery=="interior" && seek.phase==ReplayHost::SeekPhase::Held) {
                ReplayTrajectorySample sample{};const void* event{};
                if(held.tick!=208 || seek.commit_decided || !ReadPendingInteriorTask(battle_manager_,held,event)
                    || event!=completion_repeat_event_ || !ReadReplayTrajectory(battle_manager_,sample,true)
                    || sample!=completion_repeat_sample_ || boundary_samples_!=completion_repeat_callbacks_)
                    Fail("indexed_interior_queued_hold_advanced");
                return true; // Queue may still await an existing surface command.
            }
            if(seek.phase==ReplayHost::SeekPhase::Recovering) return true;
            if(seek.phase!=ReplayHost::SeekPhase::Cancelled || seek.pending || !seek.original_recovered
                || seek.commit_decided || held.tick!=indexed_recovery_B_.frame || !held.application_idle || held.pending_task
                || held.boundary!=ReplayHost::PauseBoundary::CompletedApplication) {
                Fail("indexed_recovery_B_not_recovered");return true;
            }
            ReplayTrajectorySample sample{};IndexedHudWitness hud{};
            if(!ReadReplayTrajectory(battle_manager_,sample,true) || sample!=indexed_recovery_B_
                || !ReadIndexedHud(hud) || hud!=indexed_recovery_hud_B_) {
                Fail("indexed_recovery_B_observation_mismatch");return true;
            }
            historical_rewind_ticks_=seek.recovered_execution_ticks;
            historical_rewind_intervals_=seek.recovered_execution_intervals;
            LogTrajectorySample(sample,1,L"index_recovery_B_after");LogIndexedHud(hud,L"B_after");
            Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed recovery recovered run_id={} B={} executed_ticks={} executed_intervals={} original_recovered=true commit_decided=false\n"),
                RC::to_generic_string(request_.run_id),held.tick,historical_rewind_ticks_,historical_rewind_intervals_);
            indexed_recovery_callbacks_=boundary_samples_;indexed_recovery_epoch_=held.epoch;
            indexed_recovery_frames_=held.surface_frames;indexed_recovery_started_=std::chrono::steady_clock::now();
            indexed_recovery_=IndexedRecovery::HoldingB;
        }
        if(indexed_recovery_==IndexedRecovery::HoldingB) {
            ReplayTrajectorySample sample{};IndexedHudWitness hud{};
            if(!ReadReplayTrajectory(battle_manager_,sample,true) || sample!=indexed_recovery_B_
                || !ReadIndexedHud(hud) || hud!=indexed_recovery_hud_B_ || boundary_samples_!=indexed_recovery_callbacks_
                || held.epoch!=indexed_recovery_epoch_ || held.pending_task || !held.application_idle) {
                Fail("indexed_recovery_B_hold_advanced");return true;
            }
            const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-indexed_recovery_started_).count();
            if(elapsed<500000 || held.surface_frames-indexed_recovery_frames_<30) return true;
            Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed recovery held run_id={} B={} elapsed_us={} frames={} callbacks_unchanged=true epoch_unchanged=true HUD_unchanged=true\n"),
                RC::to_generic_string(request_.run_id),held.tick,elapsed,held.surface_frames-indexed_recovery_frames_);
            indexed_recovery_=IndexedRecovery::Releasing;
        }
        if(indexed_recovery_==IndexedRecovery::Releasing) {
            if(held.surface_pending) return true;
            if(!operation || !operation(ReplayHost::SeekAction::Release,nullptr,0,&seek,nullptr,nullptr)) {
                Fail("indexed_recovery_release_rejected");return true;
            }
            if(seek.release_result!=ReplayHost::SeekReleaseResult::Completed) return true;
            const auto monitor=ResolveHorseModExport<bool (*)(void*,ReplayHost::PauseMonitor)>("horsemod_set_replay_pause_monitor");
            if(!monitor || !monitor(nullptr,nullptr)) {
                Fail("indexed_recovery_monitor_release_rejected");return true;
            }
            Output::send<LogLevel::Default>(STR("[ReplayQualification] indexed recovery released run_id={} B={} release_completed=true authored_ticks_remaining=0 resume_requested=false cleanup=native_exit\n"),
                RC::to_generic_string(request_.run_id),held.tick);
            // B is observed and held. Never bypass its protected endpoint to
            // finish a test; native exit owns index/surface/hook retirement.
            indexed_recovery_=IndexedRecovery::Exiting;index_seek_phase_=IndexSeek::Done;
            BeginNativeSessionExit();
        }
        return true;
    }
