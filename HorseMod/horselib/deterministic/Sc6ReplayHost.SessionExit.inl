// Static cooked evidence: held-menu-exit-static.json. OnRequestInputCommand
// enters619; BattleMenu plus the exit command takes952 -> 1268 -> 1323, which constructs
// its own inherited data. It reaches the existing guarded OnRequestToStop.
// Pre-stop Blueprint work mutates battle counters, so this is NEVER admitted
// while B or any historical execution/capture transaction is still owned.
bool Sc6ReplayHost::CanRequestMenuExit() const noexcept
{
    return GetCurrentThreadId()==thread_ && session_exit_hook_ && !SessionExitRequested()
        && index_.witness().phase==ReplayTickIndex::Phase::Complete
        && interior_phase_==InteriorPhase::Holding && pause_boundary_==PauseBoundary::CompletedApplication
        && application_phase_==ApplicationPhase::Idle && engine_idle() && simulation_->interval_complete()
        && !SeekOwnsExecution() && !historical_restore_ && !checkpoint_restoring_
        && capture_operation_.phase==CapturePhase::Idle && !index_checkpoint_.pending()
        && !surface_event_ && !particle_command_pending_.load()
        && !seek_retirement_pending_ && !seek_retirement_failed_ && CanRetireSeekCheckpoint();
}

bool Sc6ReplayHost::RequestMenuExit() noexcept
{
    using namespace RC::Unreal;
    const auto reject=[&](const char* check) {
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] replay menu exit rejected check={} native_retry=false\n"),RC::to_generic_string(check));
        return false;
    };
    if(!CanRequestMenuExit() || !CheckBinding()) return reject("admission");
    try {
        auto* stop=static_cast<UFunction*>(session_exit_function_);
        const auto* stop_item=FUObjectArray::IndexToObject(session_exit_function_index_);
        if(!stop || !stop_item || stop_item->GetUObject()!=stop || !stop_item->IsValid(false)
            || stop_item->GetSerialNumber()!=session_exit_function_serial_) return false;
        std::vector<UObject*> managers;UObjectGlobals::FindAllOf(STR("LuxUIGameFlowManager"),managers);
        UObject* scene{};
        for(auto* manager:managers) {
            if(!manager || !UObject::IsReal(manager)) continue;
            auto** current=manager->GetValuePtrByPropertyNameInChain<UObject*>(STR("CurrentScene"));
            if(!current || !*current || !UObject::IsReal(*current)
                || static_cast<UObject*>((*current)->GetClassPrivate())!=stop->GetOuterPrivate()) continue;
            if(scene) return false;
            scene=*current;
        }
        if(!scene) return false;
        auto* event=scene->GetFunctionByNameInChain(STR("OnRequestInputCommand"));
        if(!event || event->GetOuterPrivate()!=stop->GetOuterPrivate()
            || event->GetFunc()!=UObject::ProcessInternalInternal.get_function_address()
            || event->HasAnyFunctionFlags(EFunctionFlags::FUNC_Native)) return false;
        struct Params {
            FString menu;UObject* menu_widget{};UObject* target_widget{};
            FString command;std::array<void*,3> data{};std::int32_t controller{};
            Params():menu(STR("BattleMenu")),command(STR("LuxPauseMenu::GoBackToReplaySelect")){}
        } params;
        static_assert(sizeof(Params)==0x50);
        // The selected cooked branch consumes only MenuName and CommandName;
        // null widget arguments and the default UIDataObject never escape to
        // the parent/default branch. Check every reflected slot before native
        // construction, including the trailing parameter extent/alignment.
        const auto slot=[&](const wchar_t* name,void* address,std::size_t size,bool string) {
            auto* property=event->FindProperty(FName(name,FNAME_Find));
            return property && (!string || CastField<FStrProperty>(property)) && property->GetSize()==size
                && property->ContainerPtrToValuePtr<void>(&params)==address;
        };
        const auto extent=offsetof(Params,controller)+sizeof(params.controller);
        if((event->GetParmsSize()!=extent && event->GetParmsSize()!=sizeof(params))
            || (event->GetPropertiesSize()!=extent && event->GetPropertiesSize()!=sizeof(params))
            || !slot(STR("MenuName"),&params.menu,sizeof(params.menu),true)
            || !slot(STR("MenuWidget"),&params.menu_widget,sizeof(params.menu_widget),false)
            || !slot(STR("TargetWidget"),&params.target_widget,sizeof(params.target_widget),false)
            || !slot(STR("CommandName"),&params.command,sizeof(params.command),true)
            || !slot(STR("Param"),params.data.data(),sizeof(params.data),false)
            || !slot(STR("ControllerId"),&params.controller,sizeof(params.controller),false)) {
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] replay menu exit metadata parms={} properties={} expected_min={} expected_max={}\n"),
                event->GetParmsSize(),event->GetPropertiesSize(),extent,sizeof(params));
            return reject("parameter_contract");
        }
        // These are the existing navigator's verified native UIDataObject
        // constructor/destructor. Keep their signature checks before acquiring.
        constexpr std::array<unsigned char,8> init{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74};
        constexpr std::array<unsigned char,8> destroy{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83};
        if(std::memcmp(reinterpret_cast<void*>(image_base_+0x2ed1370),init.data(),init.size())
            || std::memcmp(reinterpret_cast<void*>(image_base_+0x2ed6a80),destroy.data(),destroy.size())) return false;
        EngineNative(image_base_,0x2ed1370,params.data.data());
        try { scene->ProcessEvent(event,&params); }
        catch(...) {
            EngineNative(image_base_,0x2ed6a80,params.data.data());
            session_exit_.phase=SessionExitPhase::Failed;
            session_exit_.failure=FailureCode::ContextUnavailable;
            return false; // Unknown native effects: no second publication.
        }
        EngineNative(image_base_,0x2ed6a80,params.data.data());
        const bool accepted=SessionExitRequested();
        if(!accepted) {
            session_exit_.phase=SessionExitPhase::Failed;
            session_exit_.failure=FailureCode::ContextUnavailable;
        }
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] replay menu exit submitted tick={} accepted={} B_transaction=false native_stop_guarded=true\n"),
            simulation_->continuation().tick,accepted);
        return accepted;
    } catch(...) { return false; }
}

// ReplayBattleScene.OnRequestToStop (cooked entry 2094) is parameterless.
// Its parent starts a fade; only the later fade callback terminates battle
// and calls ReadyToStop. The native flow manager retains CurrentScene and
// its completion delegate until that notification. No FFrame is retained.
bool Sc6ReplayHost::BindSessionExit()
{
    using namespace RC::Unreal;
    if(session_exit_hook_) return true;
    auto* function=UObjectGlobals::StaticFindObject<UFunction*>(nullptr,nullptr,
        STR("/Game/UI/GameFlow/GameScenes/Battle/ReplayBattleScene.ReplayBattleScene_C:OnRequestToStop"));
    if(!function || function->GetFunc()!=UObject::ProcessInternalInternal.get_function_address()
        || function->HasAnyFunctionFlags(EFunctionFlags::FUNC_Native) || function->GetParmsSize()!=0) return false;
    const auto* item=FUObjectArray::IndexToObject(function->GetInternalIndex());
    if(!item || item->GetUObject()!=function || !item->IsValid(false)) return false;
    std::array<int,2> weak{};
    EngineNative(image_base_,0xf7bad0,weak.data(),function);
    if(weak[0]!=function->GetInternalIndex() || !weak[1] || weak[1]!=item->GetSerialNumber()) return false;
    session_exit_function_=function;session_exit_function_index_=weak[0];session_exit_function_serial_=weak[1];
    session_exit_hook_=Hook::RegisterProcessInternalPreCallback(
        [](auto& call,UObject* context,FFrame& stack,void*) {
            auto* self=active_;
            if(!self || stack.Node()!=self->session_exit_function_) return;
            // Once an indexed session owns resources, this specific native
            // request cannot proceed through a failed cleanup or stale binding.
            call.PreventOriginalFunctionCall();
            if(self->SessionExitRequested()) return; // One deferred publication.
            self->session_exit_.requested_tick=self->simulation_->continuation().tick;
            self->session_exit_.phase=SessionExitPhase::Requested;
            bool valid=GetCurrentThreadId()==self->thread_ && self->CheckBinding();
            try {
                std::vector<UObject*> managers;
                UObjectGlobals::FindAllOf(STR("LuxUIGameFlowManager"),managers);
                UObject* owner{};
                for(auto* manager:managers) {
                    if(!manager || !UObject::IsReal(manager)) continue;
                    auto** current=manager->GetValuePtrByPropertyNameInChain<UObject*>(STR("CurrentScene"));
                    if(current && *current==context) { if(owner) {valid=false;break;} owner=manager; }
                }
                valid=valid && context && owner && UObject::IsReal(context);
                if(valid) {
                    const auto bind=[&](UObject* object,void*& pointer,std::int32_t& index,std::int32_t& serial) {
                        const auto* row=FUObjectArray::IndexToObject(object->GetInternalIndex());
                        if(!row || row->GetUObject()!=object || !row->IsValid(false)) return false;
                        std::array<int,2> id{};EngineNative(self->image_base_,0xf7bad0,id.data(),object);
                        if(id[0]!=object->GetInternalIndex() || !id[1] || id[1]!=row->GetSerialNumber()) return false;
                        pointer=object;index=id[0];serial=id[1];return true;
                    };
                    valid=bind(context,self->session_exit_scene_,self->session_exit_scene_index_,self->session_exit_scene_serial_)
                        && bind(owner,self->session_exit_manager_,self->session_exit_manager_index_,self->session_exit_manager_serial_)
                        && self->ValidateSessionExit();
                }
            } catch(...) { valid=false; }
            if(!valid) {
                self->session_exit_.phase=SessionExitPhase::Failed;
                self->session_exit_.failure=FailureCode::GenerationMismatch;
            }
            self->queued_indexed_seek_.reset();
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] native replay exit deferred tick={} admitted={} original_invocations=0\n"),
                self->session_exit_.requested_tick,valid);
        },{false,false,STR("HorseMod.ReplaySession"),STR("DeferNativeStop")});
    return session_exit_hook_!=0;
}

bool Sc6ReplayHost::UnbindSessionExit() noexcept
{
    if(session_exit_hook_ && !RC::Unreal::Hook::UnregisterCallback(session_exit_hook_)) return false;
    session_exit_hook_=0;return true;
}

bool Sc6ReplayHost::ValidateSessionExit() const noexcept
{
    using namespace RC::Unreal;
    const auto live=[](void* object,std::int32_t index,std::int32_t serial) {
        const auto* item=FUObjectArray::IndexToObject(index);
        return object && serial && item && item->GetUObject()==object && item->IsValid(false) && item->GetSerialNumber()==serial;
    };
    if(!live(session_exit_function_,session_exit_function_index_,session_exit_function_serial_)
        || !live(session_exit_scene_,session_exit_scene_index_,session_exit_scene_serial_)
        || !live(session_exit_manager_,session_exit_manager_index_,session_exit_manager_serial_)) return false;
    try {
        const auto* manager=static_cast<UObject*>(session_exit_manager_);
        auto** current=const_cast<UObject*>(manager)->GetValuePtrByPropertyNameInChain<UObject*>(STR("CurrentScene"));
        return current && *current==session_exit_scene_;
    } catch(...) { return false; }
}

bool Sc6ReplayHost::ReadSessionExit(SessionExitWitness* output) noexcept
{
    if(!output || !active_ || GetCurrentThreadId()!=active_->thread_) return false;
    *output=active_->session_exit_;return true;
}

// True means that no new application may start. False permits only the
// existing execution/hold owner to complete the work needed for cleanup.
bool Sc6ReplayHost::ServiceSessionExit() noexcept
{
    if(!SessionExitRequested()) return false;
    const auto fail=[&](FailureCode code) {
        session_exit_.failure=code;session_exit_.phase=SessionExitPhase::Failed;
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] native replay exit blocked tick={} failure={} native_stop_unpublished=true\n"),
            simulation_->continuation().tick,static_cast<unsigned>(code));
        return true;
    };
    if(session_exit_.phase==SessionExitPhase::Failed || session_exit_.phase==SessionExitPhase::Complete) return true;
    if(!ValidateSessionExit() || !CheckBinding()) return fail(FailureCode::GenerationMismatch);
    queued_indexed_seek_.reset();
    if(index_checkpoint_.pending()) {
        if(index_.witness().phase==ReplayTickIndex::Phase::Recording) index_.Cancel();
        session_exit_.phase=SessionExitPhase::Recovering;
        return false; // Its existing hold owns capture cancellation and retirement.
    }
    if(SeekOwnsExecution()) {
        const auto state=seek_.witness;
        if(!state.commit_decided && !seek_.cancel && (historical_restore_
            || (state.phase!=SeekPhase::Held && state.phase!=SeekPhase::Cancelled && state.phase!=SeekPhase::Failed))) {
            SeekWitness cancelled{};
            if(!SeekOperation(SeekAction::Cancel,nullptr,0,&cancelled)) return fail(FailureCode::UndoFailed);
            session_exit_.phase=SessionExitPhase::Recovering;
            return false;
        }
        if(state.pending || historical_restore_) return false;
        session_exit_.recovered_tick=simulation_->continuation().tick;
        SeekWitness released{};
        if(!SeekOperation(SeekAction::Release,nullptr,0,&released)) {
            if(surface_event_ || particle_command_pending_.load() || seek_retirement_pending_) return false;
            return fail(FailureCode::IllegalTransition);
        }
        if(released.release_result!=SeekReleaseResult::Completed) return false;
    }
    // Standalone capture handles remain owned by their caller. Never erase
    // borrowed/private images to make exit pass; wait for their normal release.
    if(capture_operation_.phase!=CapturePhase::Idle || historical_restore_ || checkpoint_restoring_) return false;
    session_exit_.phase=SessionExitPhase::Retiring;
    if(index_.witness().phase==ReplayTickIndex::Phase::Recording) index_.Cancel();
    if(!simulation_->interval_complete()) return false;
    if(index_.witness().phase!=ReplayTickIndex::Phase::Empty) {
        ReplayTickIndex::Witness released{};
        if(!IndexOperation(IndexAction::Release,&released,0)) return fail(FailureCode::IllegalTransition);
        if(CanRetireSeekCheckpoint()) FinishIndexAtApplicationBoundary();
        if(seek_retirement_pending_ && CanRetireSeekCheckpoint()) AdvanceSeekRetirement();
        if(seek_retirement_failed_) return fail(FailureCode::PresentationFailed);
        if(index_.witness().phase!=ReplayTickIndex::Phase::Empty) return CanRetireSeekCheckpoint();
    }
    if(Sc6ReplayParticleConfiguration::retirement_pending())seek_retirement_pending_=true;
    if(seek_retirement_pending_) return false;
    if(interior_phase_==InteriorPhase::Failed) return fail(FailureCode::IllegalTransition);
    if(interior_phase_==InteriorPhase::Holding) {
        session_exit_.phase=SessionExitPhase::ReleasingHold;
        if(!interior_resume_requested_ && !Resume().ok()) {
            if(surface_event_ || particle_command_pending_.load()) return false;
            return fail(FailureCode::IllegalTransition);
        }
        return false;
    }
    if(interior_phase_!=InteriorPhase::Idle && interior_phase_!=InteriorPhase::Resumed) return false;
    if(application_phase_!=ApplicationPhase::Idle || !engine_idle()) return false;
    session_exit_.phase=SessionExitPhase::Detaching;
    if(!session_exit_.recovered_tick) session_exit_.recovered_tick=simulation_->continuation().tick;
    // Stop's acceptance is insufficient: the hook owner and final application
    // tail must both be gone before the deferred Blueprint can destroy actors.
    if(!DeterministicHookSet::SetReplayExecutorEnabled(false,false,nullptr) || !StopForDestruction()) return true;
    if(!ValidateSessionExit()) return fail(FailureCode::GenerationMismatch);
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] application executor stopped intervals={} idle=true disabled=true\n"),completed_applications_);
    session_exit_.phase=SessionExitPhase::Complete;
    auto* scene=static_cast<RC::Unreal::UObject*>(session_exit_scene_);
    auto* function=static_cast<RC::Unreal::UFunction*>(session_exit_function_);
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] native replay exit cleanup completed requested_tick={} recovered_tick={} index_empty=true host_detached=true native_stop_invocations=1\n"),
        session_exit_.requested_tick,session_exit_.recovered_tick);
    // UnbindSessionExit removed the interceptor. Mark publication before the
    // call, since reentrant native scene work must never publish this twice.
    try { scene->ProcessEvent(function,nullptr); }
    catch(...) { session_exit_.phase=SessionExitPhase::Failed;session_exit_.failure=FailureCode::ContextUnavailable; }
    return true;
}
