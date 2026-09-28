struct FreshChild {
    enum class HistoryPhase {Empty,Writing,Ready,Failed};
    enum class AnimationPhase {Empty,Calling,Ready,Failed};
    enum class StrongPhase {Private,Publishing,Published};
    enum class ExecutionOutcome {None,Recovered,Committed};
    Sc6ReplayTraceChildFactory::Journal factory;
    Sc6ReplayObjectLease lease;
    State state{};
    Ref historical{};
    Ref cached_parent{}; // Native weak owner installed by history preparation; survives payload death as metadata.
    Array strong{};
    std::array<Array,6> weak{};
    Retirement retirement;
    std::uint64_t retirement_serial{};
    std::size_t allowance{};
    bool prepared{},retired{},identity_retained{};
    bool execution_settled{},native_dead{},adopted{};
    ExecutionOutcome execution_outcome{ExecutionOutcome::None};
    HistoryPhase history_phase{HistoryPhase::Empty};
    AnimationPhase animation_phase{AnimationPhase::Empty};
    StrongPhase strong_phase{StrongPhase::Private};
    struct AnimationDestination {void* historical{};void* destination{};};
    std::vector<AnimationDestination> animation_arrays;
    void* animation_proxy{};void* animation_metadata{};
    std::size_t owned_bytes() const noexcept {return sizeof(*this)+lease.owned_bytes()+allowance+animation_arrays.capacity()*sizeof(AnimationDestination);}
};
#include "Sc6ReplayTraceFreshHistory.inl"
#include "Sc6ReplayTraceFreshAnimation.inl"
#include "Sc6ReplayTraceFreshRender.inl"
Status ValidateFreshChildExecution(const FreshChild& child,const Sc6ReplayTraceState& current,
    const ReplayGpuCompletion& completion,std::uint64_t submission_floor,bool& dead) const {
    if(thread_!=GetCurrentThreadId() || current.thread_!=thread_ || base_!=current.base_ || world_!=current.world_
        || !child.prepared || child.retired || child.adopted || !child.identity_retained
        || child.execution_outcome!=FreshChild::ExecutionOutcome::None
        || child.factory.phase!=Sc6ReplayTraceChildFactory::Phase::Returned
        || child.history_phase!=FreshChild::HistoryPhase::Ready || child.animation_phase!=FreshChild::AnimationPhase::Ready
        || child.strong_phase!=FreshChild::StrongPhase::Published || child.strong.count!=0
        || child.strong.data!=reinterpret_cast<const std::byte*>(&child.factory.reference) || child.strong.capacity!=1
        || !completion.retired() || completion.result()!=ReplayGpuCompletion::Result::Complete
        || completion.submitted_serial()<=submission_floor)return Fail("fresh_child_execution_receipt");
    const auto ref=child.state.ref;
    if(!ref.controller || ref.state!=ref.controller+16 || child.factory.reference.state!=ref.state
        || child.factory.reference.controller!=ref.controller || child.state.attachment.object
        || !child.state.actor.object || !child.state.mesh.object
        || At<std::uintptr_t>(ref.controller,0)!=base_+0x3362590 || At<int>(ref.controller,12)<1)
        return Fail("fresh_child_execution_identity");
    auto status=ValidateBindings();if(!status.ok())return status;
    status=current.ValidateBindings();if(!status.ok())return status;
    const auto strong=At<int>(ref.controller,8);
    unsigned entries{};
    for(std::size_t i=0;i<current.dynamic_count_;++i)if(current.dynamic_[i].kind==DynamicImage::Kind::ChildStrongReferences) {
        const auto& image=current.dynamic_[i];const auto n=At<int>(const_cast<std::byte*>(image.header.data()),8);
        if(n<0 || std::size_t(n)>image.bytes.size()/16)return Fail("fresh_child_execution_strong_shape");
        for(int j=0;j<n;++j) {
            const auto row=At<Ref>(const_cast<std::byte*>(image.bytes.data()),std::size_t(j)*16);
            if(row.state==ref.state || row.controller==ref.controller) {
                if(row.state!=ref.state || row.controller!=ref.controller)return Fail("fresh_child_execution_strong_pair");
                ++entries;
            }
        }
    }
    const auto state=std::find_if(current.states_.begin(),current.states_.end(),[&](const auto& s) {
        return s.ref.state==ref.state || s.ref.controller==ref.controller;
    });
    if(strong==0) {
        // The operation's weak anchor retains only this controller header.
        // Native lifetime teardown destroys actor/components before releasing
        // the final strong owner. Never inspect their payloads after death.
        if(entries || state!=current.states_.end() || Live(child.state.actor) || Live(child.state.mesh))
            return Fail("fresh_child_execution_native_death");
        dead=true;return Status::success();
    }
    if(strong!=1 || entries!=1 || At<int>(ref.controller,12)<2 || state==current.states_.end()
        || state->ref.state!=ref.state || state->ref.controller!=ref.controller
        || state->actor!=child.state.actor || state->mesh!=child.state.mesh || state->animation!=child.state.animation
        || state->attachment!=child.state.attachment || !child.lease.Validate().ok())return Fail("fresh_child_execution_live_owner");
    const auto parent=At<Ref>(ref.state,0xa0);
    if(parent.state!=child.cached_parent.state || parent.controller!=child.cached_parent.controller)
        return Fail("fresh_child_execution_parent_changed");
    dead=false;return Status::success();
}
Status FreshChildMeshBinding(const FreshChild& child,Sc6ReplayVfxState::ReconstructionBinding& output) const {
    if(thread_!=GetCurrentThreadId())return Fail("fresh_child_mesh_binding_thread");
    const auto status=child.lease.Validate();if(!status.ok())return status;
    return FreshChildRenderBinding(child,output);
}
Status FreshChildRenderBinding(const FreshChild& child,Sc6ReplayVfxState::ReconstructionBinding& output) const {
    // Reuse the plain component identity/lease layout, not VFX ownership proof.
    // The historical mesh is a retained indexed identity and is never read.
    if(!captured_ || !child.prepared || child.retired || !child.identity_retained || !child.lease.registered()
        || child.factory.phase!=Sc6ReplayTraceChildFactory::Phase::Returned
        || child.history_phase!=FreshChild::HistoryPhase::Ready || child.animation_phase!=FreshChild::AnimationPhase::Ready)
        return Fail("fresh_child_mesh_binding_phase");
    // Full GC lease membership is checked on the game thread above. At the
    // held render boundary use registered retention and exact indexed live
    // identities; registry admission must never be attempted off-thread.
    const auto old=std::find_if(states_.begin(),states_.end(),[&](const auto& state) {
        return state.ref.state==child.historical.state && state.ref.controller==child.historical.controller;
    });
    if(old==states_.end() || !old->mesh.object || !child.state.mesh.object || old->attachment.object || child.state.attachment.object
        || child.state.ref.state!=child.factory.reference.state || child.state.ref.controller!=child.factory.reference.controller
        || !child.state.ref.controller || child.state.ref.state!=child.state.ref.controller+16
        || At<std::uintptr_t>(child.state.ref.controller,0)!=base_+0x3362590
        || At<int>(child.state.ref.controller,8)!=1 || At<int>(child.state.ref.controller,12)<2
        || !Live(child.state.actor) || !Live(child.state.mesh) || !Live(child.state.animation)
        || At<Object*>(child.state.ref.state,8)!=child.state.actor.object
        || At<Object*>(child.state.actor.object,0x398)!=child.state.mesh.object
        || At<Object*>(child.state.mesh.object,0x190)!=child.state.actor.object
        || At<Object*>(child.state.mesh.object,0xab0)!=child.state.animation.object
        || At<std::uintptr_t>(child.state.mesh.object,0)!=base_+0x38829c0)
        return Fail("fresh_child_mesh_binding_identity");
    if(std::none_of(roots_.begin(),roots_.end(),[&](const auto& root) {
        return reinterpret_cast<std::uintptr_t>(root.component.object)==child.factory.component && Live(root.component);
    }))return Fail("fresh_child_mesh_binding_root");
    output={{reinterpret_cast<std::uintptr_t>(old->mesh.object),reinterpret_cast<std::uintptr_t>(child.state.mesh.object),
        {old->mesh.index,old->mesh.serial},{child.state.mesh.index,child.state.mesh.serial}},&child.lease};
    return Status::success();
}
bool FreshChildDormantRenderSource(const FreshChild& child,unsigned& source_id,std::uintptr_t& mesh_asset) const {
    Sc6ReplayVfxState::ReconstructionBinding binding;
    if(!FreshChildMeshBinding(child,binding).ok())return false;
    unsigned visibility{};
    auto* source=reinterpret_cast<void*>(binding.identity.source);
    auto* mesh=child.state.mesh.object;
    // Retained A metadata establishes logical hidden state. Never inspect the
    // expired source payload or infer hidden state from a missing scene row.
    if(!RetainedField(values_,source,0x240,visibility) || (visibility&0x10)
        || !RetainedField(bindings_,source,0x420,source_id) || !source_id || source_id==UINT_MAX
        || !RetainedField(bindings_,source,0x910,mesh_asset) || !mesh_asset)return false;
    return At<std::uintptr_t>(mesh,0x910)==mesh_asset && !(At<unsigned>(mesh,0x240)&0x10)
        && !(At<unsigned>(mesh,0x3fc)&2) && !(At<unsigned>(mesh,0x188)&0xc00000e0u)
        && !At<void*>(mesh,0x790) && !At<void*>(mesh,0xa18);
}
Status ConstructFreshChild(const ChildSource& source,const Sc6ReplayTraceState& current,
    std::size_t budget,FreshChild& output) const {
    using Factory=Sc6ReplayTraceChildFactory;
    if(output.factory.phase!=Factory::Phase::Empty || output.prepared || output.allowance
        || base_!=current.base_ || world_!=current.world_ || thread_!=GetCurrentThreadId())return Fail("fresh_child_context");
    auto status=current.ValidateValues();if(!status.ok())return status;
    unsigned matches{};
    status=VisitHistoricalChildSources([&](const ChildSource& candidate) {
        if(candidate.historical.state==source.historical.state && candidate.historical.controller==source.historical.controller
            && candidate.component==source.component && candidate.parts==source.parts && candidate.kind==source.kind
            && candidate.mesh_asset==source.mesh_asset && candidate.animation_class==source.animation_class
            && candidate.parts_id==source.parts_id && candidate.kind_id==source.kind_id && candidate.charge==source.charge)++matches;
        return Status::success();
    });
    if(!status.ok())return status;
    if(matches!=1 || At<int>(source.kind,0x48)!=-1)return Fail("fresh_child_source");
    // First admitted native shape has no VFX attachment. An attachment needs
    // its additional manager/component acquisition journal before admission.
    const auto historical=std::find_if(states_.begin(),states_.end(),[&](const auto& s) {
        return s.ref.state==source.historical.state && s.ref.controller==source.historical.controller;
    });
    if(historical==states_.end() || historical->attachment.object)return Fail("fresh_child_attachment");
    status=current.CheckRetirementWorld();if(!status.ok())return status;
    const auto actor=std::find_if(current.states_.begin(),current.states_.end(),[&](const auto& s) {
        return Live(s.actor) && s.actor.object && At<std::uintptr_t>(s.actor.object,0)==base_+0x3361660;
    });
    if(actor==current.states_.end())return Fail("fresh_child_creation_actor");
    auto* actor_class=actor->actor.object->GetClassPrivate();
    auto* factory_class=At<void*>(reinterpret_cast<void*>(base_),0x4159d58);
    auto* begin_play=actor->actor.object->GetFunctionByNameInChain(L"ReceiveBeginPlay");
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] fresh trace creation admission actor={:x} class={:x} factory_class={:x} event={} flags={:x} script_bytes={} factory_called=false B_retained=true\n"),
        reinterpret_cast<std::uintptr_t>(actor->actor.object),reinterpret_cast<std::uintptr_t>(actor_class),
        reinterpret_cast<std::uintptr_t>(factory_class),begin_play?begin_play->GetFullName():STR("<missing>"),
        begin_play?static_cast<unsigned>(begin_play->GetFunctionFlags()):0u,begin_play?begin_play->GetScript().Num():-1);
    if(reinterpret_cast<void*>(actor_class)!=factory_class)return Fail("fresh_child_creation_class");
    // Actor.ReceiveBeginPlay is a protected Blueprint event in this binary;
    // the destruction events retain their separately verified public flags.
    if(!EmptyEvent(actor->actor.object,L"ReceiveBeginPlay",L"Function /Script/Engine.Actor:ReceiveBeginPlay",0x08080800))
        return Fail("fresh_child_creation_callbacks");
    // Diagnostic reservation, not measured production allocation accounting.
    // This path remains unqualified until native/GPU peak costs are measured.
    constexpr std::size_t allowance=16u*1024*1024;
    if(budget<allowance+sizeof(output)+4096)return Status::failure(FailureCode::CapacityExceeded);
    // Validate the same native retirement entry signatures before the owner is
    // eligible for the host's preparation-cleanup path.
    constexpr unsigned char destroy[]{0x48,0x89,0x5c,0x24,8,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
    constexpr unsigned char weak[]{0x40,0x55,0x41,0x55,0x41,0x56,0x48,0x83,0xec,0x30,0x8b,0x69,8,0x4c,0x8b,0xea};
    constexpr unsigned char strong[]{0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,8,0x53,0x48,0x83,0xec,0x50};
    if(std::memcmp(reinterpret_cast<void*>(base_+0x1c11c60),destroy,sizeof(destroy))
        || std::memcmp(reinterpret_cast<void*>(base_+0x213cc20),weak,sizeof(weak))
        || std::memcmp(reinterpret_cast<void*>(base_+0x1d0a460),strong,sizeof(strong)))return Fail("fresh_child_retirement_signature");
    if(!Factory::PreservesSharedMeshBounds(base_,reinterpret_cast<std::uintptr_t>(source.mesh_asset)))
        return Fail("fresh_child_shared_mesh_bounds");
    output.historical=source.historical;
    output.factory.mesh_asset=reinterpret_cast<std::uintptr_t>(source.mesh_asset);
    output.allowance=allowance;
    if(!Factory::Construct(base_,reinterpret_cast<std::uintptr_t>(source.component),reinterpret_cast<std::uintptr_t>(source.parts),source.kind_id,output.factory))
        return Fail("fresh_child_factory");
    auto& state=output.state;
    state.ref={output.factory.reference.state,output.factory.reference.controller};
    if(At<std::uintptr_t>(state.ref.controller,0)!=base_+0x3362590 || At<int>(state.ref.controller,8)!=1
        || At<int>(state.ref.controller,12)!=1 || current.Contains(state.ref))return Fail("fresh_child_controller");
    state.actor=Identify(At<Object*>(state.ref.state,8));
    if(!state.actor.object || At<Object*>(state.ref.state,0xb8))return Fail("fresh_child_actor");
    state.mesh=Identify(At<Object*>(state.actor.object,0x398));
    if(!state.mesh.object || At<Object*>(state.actor.object,0x168)!=state.mesh.object
        || At<Object*>(state.mesh.object,0x190)!=state.actor.object || At<Object*>(state.mesh.object,0x910)!=source.mesh_asset)
        return Fail("fresh_child_mesh");
    state.animation=Identify(reinterpret_cast<Object*(*)(void*)>(base_+0x1d9d460)(state.mesh.object));
    if(!state.animation.object || reinterpret_cast<Object*>(state.animation.object->GetClassPrivate())!=source.animation_class)
        return Fail("fresh_child_animation");
    // A's saved addresses may already have been recycled; its vector payloads
    // are retained separately. Protect actual live B ownership here, not stale
    // historical addresses. Factory identity/leases still prove a new owner.
    status=current.CheckChildStorageAndLifecycle(state,current,current);if(!status.ok())return status;
    std::array<void*,3> objects{state.actor.object,state.mesh.object,state.animation.object};
    status=output.lease.Acquire(base_,objects,budget-output.owned_bytes());if(!status.ok())return status;
    output.strong={reinterpret_cast<std::byte*>(&output.factory.reference),1,1};
    output.retirement.count=1;output.retirement.children[0]=state;output.retirement.roots[0]=source.component;
    output.retirement.storage=Retirement::Storage::Private;
    output.retirement.private_strong_headers[0]=&output.strong;
    output.retirement.private_weak_count=current.roots_.size()*3;
    if(output.retirement.private_weak_count>output.weak.size())return Fail("fresh_child_weak_capacity");
    for(std::size_t i=0;i<output.retirement.private_weak_count;++i)output.retirement.private_weak_headers[i]=&output.weak[i];
    output.retirement.step=Retirement::Step::Ready;output.prepared=true;
    if(!reinterpret_cast<ReplayTraceWeakController*>(state.ref.controller)->RetainLiveIdentity(base_+0x3362590))
        return Fail("fresh_child_identity_retention");
    output.identity_retained=true;
    return current.ValidateValues();
}
Status RetireFreshChild(FreshChild& child) const {
    if(child.retired)return Status::success();
    if(!child.prepared || child.factory.phase!=Sc6ReplayTraceChildFactory::Phase::Returned
        || child.strong_phase!=FreshChild::StrongPhase::Private
        || child.history_phase==FreshChild::HistoryPhase::Writing || child.history_phase==FreshChild::HistoryPhase::Failed
        || child.animation_phase==FreshChild::AnimationPhase::Calling || child.animation_phase==FreshChild::AnimationPhase::Failed)
        return Fail("fresh_child_retirement_context");
    auto status=ValidateValues();if(!status.ok())return status;
    if(child.retirement.step==Retirement::Step::Finished)return status;
    if(!child.lease.Validate().ok() || !RetireChildrenNative(child.retirement))return Fail("fresh_child_retirement");
    return ValidateValues(); // The host still owes a fresh GPU drain and lease release.
}
// The host calls this only after the enclosing GPU transaction has reached
// Released (and completed native retirement on commit). Prepared records the
// settled outcome before that final drain; it never pins native strong state.
Status ReleaseExecutedFreshChildOwnership(FreshChild& child,bool commit) const {
    const auto outcome=commit?FreshChild::ExecutionOutcome::Committed:FreshChild::ExecutionOutcome::Recovered;
    if(thread_!=GetCurrentThreadId() || !child.prepared || !child.execution_settled || child.execution_outcome!=outcome
        || child.strong_phase!=FreshChild::StrongPhase::Published || child.strong.count || (!commit && !child.native_dead))
        return Fail("fresh_child_execution_release_phase");
    if(child.retired || child.adopted)return !child.identity_retained?Status::success():Fail("fresh_child_execution_release_identity");
    if(child.identity_retained) {
        const auto ref=child.state.ref;
        auto* control=reinterpret_cast<ReplayTraceWeakController*>(ref.controller);
        if(!control || ref.state!=ref.controller+16 || control->type!=base_+0x3362590 || control->weak<1)
            return Fail("fresh_child_execution_release_controller");
        if(child.native_dead) {
            if(!control->Expired(base_+0x3362590) || Live(child.state.actor) || Live(child.state.mesh))
                return Fail("fresh_child_execution_release_death");
        } else if(control->strong!=1 || control->weak<2 || !child.lease.Validate().ok() || !OwnsNativeChild(ref))
            return Fail("fresh_child_execution_release_adoption");
        if(!control->ReleaseIdentity(base_+0x3362590,&DeleteExpiredController))return Fail("fresh_child_execution_release_weak");
        child.identity_retained=false;
    }
    const auto status=child.lease.Release();if(!status.ok())return status;
    child.allowance=0;child.retired=child.native_dead;child.adopted=!child.native_dead;
    return Status::success();
}
// The host calls this only after the child's retirement has a newer completed
// GPU event. Releasing the weak identity cannot revive or destroy trace state.
Status ReleaseFreshChildOwnership(FreshChild& child) const {
    if(thread_!=GetCurrentThreadId() || !child.prepared || child.retirement.step!=Retirement::Step::Finished)
        return Fail("fresh_child_ownership_release");
    if(child.identity_retained) {
        auto* control=reinterpret_cast<ReplayTraceWeakController*>(child.state.ref.controller);
        if(!control->Expired(base_+0x3362590)
            || !control->ReleaseIdentity(base_+0x3362590,&DeleteExpiredController))
            return Fail("fresh_child_identity_release");
        child.identity_retained=false;
    }
    return child.lease.Release();
}
