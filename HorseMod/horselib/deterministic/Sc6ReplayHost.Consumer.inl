bool Sc6ReplayHost::ReadConsumerMutationCandidate(void* source_task,void** mesh,void** source) noexcept
{
    if(!mesh || !source)return false;
    *mesh=*source=nullptr;auto* self=active_;
    if(!self || GetCurrentThreadId()!=self->thread_ || !self->historical_restore_
        || !self->historical_restore_->execution)return false;
    const auto& execution=*self->historical_restore_->execution;
    return execution.ownership.phase()==ReplaySeekOwnership::Phase::ExecutionActive
        && execution.prerequisite_guard && execution.prerequisite_guard->MutationCandidate(source_task,*mesh,*source);
}

bool Sc6ReplayHost::ArmConsumerTaskProbe(const Checkpoint* image,std::uint64_t tick,std::uint32_t hold_ms) noexcept
{
    auto* self=active_;
    if(!self || GetCurrentThreadId()!=self->thread_ || self->consumer_hold_ || !image
        || !image->valid || !image->traces || image->boundary!=PauseBoundary::CompletedApplication
        || image->session!=self->checkpoint_session_ || image->execution.tick!=self->simulation_->continuation().tick
        || tick!=image->execution.tick+1 || !hold_ms || hold_ms>1000
        || !self->engine_idle() || !self->world_idle() || self->engine_post_deferred_
        || self->application_phase_!=ApplicationPhase::Idle || self->historical_restore_
        || self->SeekOwnsExecution() || !self->CheckBinding())return false;
    try {
        auto state=std::make_unique<ConsumerHoldState>();
        state->checkpoint=image->shared_from_this();state->host=self;
        state->target=tick;state->hold_ms=hold_ms;
        std::array<Sc6ReplayTraceState::RootConsumer,2> roots{};
        if(!image->traces->ReadRootConsumers(roots))return false;
        const auto convert=[](const auto& id) {
            return NativeReplayTraceTaskGuard::Identity{id.object,id.index,id.serial};
        };
        for(std::size_t i=0;i<roots.size();++i) {
            auto& b=state->bindings[i];const auto& root=roots[i];
            b.component=convert(root.component);b.manager=convert(root.manager);
            b.chara=convert(root.chara);b.scene=convert(root.scene);
            b.world={reinterpret_cast<std::uintptr_t>(self->world_),self->object_index_,self->object_serial_};
        }
        // Its retained checkpoint is already in retained_checkpoints_; charge
        // the additional guard and continuation without dropping that lease.
        if(state->owned_bytes()>self->memory_limit_
            || self->AdmissionBytes()>self->memory_limit_-state->owned_bytes())return false;
        if(!state->guard.Bind(self->image_base_,reinterpret_cast<std::uintptr_t>(self->world_))) {
            RC::Output::send<RC::LogLevel::Warning>(STR(
                "[HorseMod] consumer guard setup rejected domain={} activation=false\n"),state->guard.domain_failure());
            return false;
        }
        for(std::size_t i=0;i<roots.size();++i)
            if(!state->guard.ReadContract(state->bindings[i],state->contracts[i])) {
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] consumer root contract rejected ordinal={}\n"),i);
                return false;
            }
        Sc6ReplayTaskGroup::ConsumerAdmissionHooks hooks{};
        hooks.context=state.get();
        hooks.acquire=[](void* context,Sc6ReplayTaskGroup::ConsumerTask& task) noexcept {
            auto& s=*static_cast<ConsumerHoldState*>(context);
            using Admission=Sc6ReplayTaskGroup::ConsumerAdmission;
            if(s.acquired || s.host->simulation_->continuation().tick!=s.target)return Admission::Forward;
            for(std::size_t i=0;i<s.bindings.size();++i)
                if(reinterpret_cast<std::uintptr_t>(task.function)==s.bindings[i].component.object+0x110) {
                    if(!s.host->executor_.consumer_boundary(true))return Admission::Reject;
                    if(!s.guard.Acquire(s.bindings[i],s.contracts[i],task)) {
                        RC::Output::send<RC::LogLevel::Warning>(STR(
                            "[HorseMod] consumer admission rejected tick={} reason={} domain={} native_started=false recovery=false\n"),
                            s.target,s.guard.admission_failure(),s.guard.domain_failure());
                        return Admission::Reject;
                    }
                    s.acquired=true;s.held_epoch=task.application_epoch;
                    return Admission::Hold;
                }
            return Admission::Forward;
        };
        hooks.validate=[](void* context,const auto& task) noexcept {
            return static_cast<ConsumerHoldState*>(context)->guard.Validate(task);
        };
        hooks.begin_execution=[](void* context,const auto& task) noexcept {
            return static_cast<ConsumerHoldState*>(context)->guard.BeginExecution(task);
        };
        hooks.completed=[](void* context,const auto& task) noexcept {
            auto& s=*static_cast<ConsumerHoldState*>(context);
            s.guard.Completed(task);s.completed=true;
        };
        self->consumer_hold_=std::move(state);
        self->executor_.BindConsumerAdmission(hooks);
        return true;
    } catch(...) {return false;}
}

bool Sc6ReplayHost::SuspendConsumerTask()
{
    const auto* task=executor_.task_groups().held_consumer_task();
    if(!task)return false;
    if(GetCurrentThreadId()!=thread_ || !executor_.consumer_boundary(false)
        || !consumer_hold_ || !consumer_hold_->acquired || consumer_hold_->suspended
        || engine_post_deferred_ || !defer_engine_post_ || !complete_engine_post_)
        __fastfail(FAST_FAIL_INVALID_ARG);
    auto& state=*consumer_hold_;
    LARGE_INTEGER qpc{};
    if(!QueryPerformanceCounter(&qpc) || !defer_engine_post_(this))__fastfail(FAST_FAIL_INVALID_ARG);
    state.qpc=qpc.QuadPart;
    state.wall_reference=*reinterpret_cast<double*>(image_base_+0x43b3778);
    state.suspended=true;engine_post_deferred_=true;
    RC::Output::send<RC::LogLevel::Default>(STR(
        "[HorseMod] consumer task held tick={} epoch={} owner_index={} owner_generation={} native_started=false recovery=false\n"),
        state.target,state.held_epoch,task->owner_index,task->owner_generation);
    return true;
}

bool Sc6ReplayHost::AdvanceConsumerTask()
{
    if(GetCurrentThreadId()!=thread_ || !executor_.consumer_boundary(false)
        || !consumer_hold_ || !consumer_hold_->suspended || !engine_post_deferred_
        || application_phase_!=ApplicationPhase::Engine || engine_idle() || depth_)
        __fastfail(FAST_FAIL_INVALID_ARG);
    auto& state=*consumer_hold_;
    ++state.polls;
    if(state.failed)return false; // Ownership remains retained; no timeout cancellation.
    LARGE_INTEGER qpc{};
    if(!QueryPerformanceCounter(&qpc))__fastfail(FAST_FAIL_INVALID_ARG);
    const double seconds=static_cast<double>(qpc.QuadPart-state.qpc)
        * *reinterpret_cast<double*>(image_base_+0x415cd90);
    if(seconds*1000.0<state.hold_ms)return false;
    auto* image=reinterpret_cast<void*>(image_base_);
    if(EngineField<bool>(image,0x416a943) || EngineField<bool>(image,0x416a944)
        || !(EngineField<std::uint8_t>(engine_,0x648)&0x40)
        || EngineField<float>(engine_,0x64c)!=60.0f || !executor_.RequestConsumerResume()) {
        state.failed=true;
        RC::Output::send<RC::LogLevel::Warning>(STR(
            "[HorseMod] consumer resume rejected tick={} ownership_retained=true recovery=false\n"),state.target);
        return false;
    }
    EngineField<double>(image,0x43b3778)=state.wall_reference+seconds;
    ++depth_;
    while(!engine_idle()) {
        AdvanceEngine();
        if(executor_.task_groups().held_consumer_task()) {
            --depth_;state.failed=true;return false;
        }
    }
    --depth_;
    if(!state.completed || !complete_engine_post_(this))__fastfail(FAST_FAIL_INVALID_ARG);
    engine_post_deferred_=false;
    return true;
}

bool Sc6ReplayHost::RetireConsumerProbe()
{
    if(!consumer_hold_)return true;
    if(!consumer_hold_->completed) {
        // A selected task that never occurred is a failed diagnostic, not a
        // reduced-coverage success. Stop at the first completed target boundary.
        return simulation_->continuation().tick<consumer_hold_->target;
    }
    if(!engine_idle() || !world_idle() || engine_post_deferred_
        || executor_.task_groups().held_consumer_task() || !consumer_hold_->guard.Stop())return false;
    executor_.BindConsumerAdmission({});
    RC::Output::send<RC::LogLevel::Default>(STR(
        "[HorseMod] consumer suspension complete tick={} epoch={} polls={} native_executions=1 application_complete=true recovery=false\n"),
        consumer_hold_->target,consumer_hold_->held_epoch,consumer_hold_->polls);
    consumer_hold_.reset();
    return true;
}

Status Sc6ReplayHost::ArmHistoricalConsumerGuard()
{
    if(!historical_restore_ || !historical_restore_->execution || consumer_hold_
        || !engine_idle() || !executor_.idle() || !executor_.arena_empty())
        return Status::failure(FailureCode::IllegalTransition);
    auto& transaction=*historical_restore_;auto& execution=*transaction.execution;
    if(execution.prerequisite_guard || execution.participants[6]!=HistoricalRestore::Execution::Participant::HandedOff)
        return Status::failure(FailureCode::IllegalTransition);
    using Guard=NativeReplayPrerequisiteGuard;
    std::array<Sc6ReplayTraceState::RootConsumer,2> roots{};
    if(!transaction.TargetTraces().ReadRootConsumers(roots))return Status::failure(FailureCode::UnsupportedContent);
    std::array<Guard::Identity,2> sources{};
    std::array<NativeReplayTraceTaskGuard::Binding,2> bindings{};
    const auto convert=[](const auto& id){return NativeReplayTraceTaskGuard::Identity{id.object,id.index,id.serial};};
    for(std::size_t i=0;i<roots.size();++i) {
        const auto& root=roots[i];sources[i]={root.component.object,root.component.index,root.component.serial};
        bindings[i]={convert(root.component),convert(root.manager),convert(root.chara),convert(root.scene),
            {reinterpret_cast<std::uintptr_t>(world_),object_index_,object_serial_}};
    }
    struct Inventory {
        const Sc6ReplaySchedulerState* scheduler;
        std::span<const Guard::Identity> sources;
        std::array<Guard::Contract,128> contracts{};
        std::size_t count{};
        Status status=Status::success();
    } inventory{&transaction.TargetScheduler(),sources};
    const auto visited=transaction.TargetTraces().VisitPrimaryTicks(&inventory,+[](void* p,std::uintptr_t mesh) {
        auto& inventory=*static_cast<Inventory*>(p);
        if(inventory.count==inventory.contracts.size()) {
            inventory.status=Status::failure(FailureCode::CapacityExceeded);return false;
        }
        inventory.status=inventory.scheduler->ReadPrerequisiteConsumer(mesh,inventory.sources,inventory.contracts[inventory.count]);
        if(!inventory.status.ok())return false;
        ++inventory.count;return true;
    });
    if(!inventory.status.ok())return inventory.status;
    if(!visited.ok())return visited;
    if(!inventory.count)return Status::success();
    if(AdmissionRemaining()<sizeof(Guard))return Status::failure(FailureCode::CapacityExceeded);
    auto guard=std::make_unique<Guard>();
    if(!guard->Configure(image_base_,executor_.task_groups(),{inventory.contracts.data(),inventory.count},bindings)
        || !guard->Arm())return Status::failure(FailureCode::UnsupportedContent);
    executor_.BindConsumerAdmission(guard->Hooks());
    execution.prerequisite_guard=std::move(guard);
    return Status::success();
}

Status Sc6ReplayHost::ArmHistoricalFinishGuard()
{
    if(!historical_restore_ || !historical_restore_->execution)
        return Status::failure(FailureCode::IllegalTransition);
    auto& transaction=*historical_restore_;auto& execution=*transaction.execution;
    if(execution.finish_armed || !execution.ownership.retains_undo())return Status::failure(FailureCode::IllegalTransition);
    ReplayVfxFinishDispatch::Binding binding{};
    auto status=transaction.manager.ReadFinishDispatchBinding(binding);
    if(!status.ok())return status;
    status=ValidateParticleCompletionOwnership(transaction.target->vfx,transaction.TargetTraces());
    if(!status.ok())return status;
    execution.finish_context={image_base_,&transaction.TargetTraces()};
    if(binding.base!=image_base_ || !execution.finish_guard.Arm(binding,&execution.finish_context,&CheckParticleCompletionReceiver))
        return Status::failure(FailureCode::UnsupportedContent);
    execution.finish_armed=true;
    return Status::success();
}

bool Sc6ReplayHost::HistoricalConsumerAbortPending() const noexcept
{
    if(!historical_restore_ || !historical_restore_->execution)return false;
    const auto& execution=*historical_restore_->execution;
    const auto phase=execution.ownership.phase();
    return execution.prerequisite_guard && execution.prerequisite_guard->changed()
        && phase==ReplaySeekOwnership::Phase::ExecutionActive;
}

bool Sc6ReplayHost::StopHistoricalConsumerGuard()
{
    if(!historical_restore_ || !historical_restore_->execution)return true;
    if(!engine_idle() || !executor_.idle() || !executor_.arena_empty()
        || !historical_restore_->execution->finish_guard.Stop())return false;
    historical_restore_->execution->finish_armed=false;
    auto& guard=historical_restore_->execution->prerequisite_guard;
    if(!guard)return true;
    if(!engine_idle() || !executor_.idle() || !executor_.arena_empty() || !guard->Stop())return false;
    executor_.BindConsumerAdmission({});guard.reset();return true;
}
