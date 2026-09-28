Status Sc6ReplayHost::PrepareFreshParticleOwners()
{
    auto& operation=*historical_restore_;
    if(operation.particle_inventory_ready)return Status::success();
    if(!operation.undo.valid || !operation.target || !restore_preparation_driving_)
        return Status::failure(FailureCode::IllegalTransition);
    if(operation.target->ground && !operation.target->ground->ValidateCapturedOwners().ok()
        && !operation.fresh_ground_roots_prepared) {
        if(!operation.fresh_ground_roots.empty())return Status::failure(FailureCode::IllegalTransition);
        for(std::size_t i=0;i<operation.target->ground->root_count();++i) {
            if(sizeof(Sc6ReplayGroundDebrisState::ColdRoot)+1024*1024+32768>AdmissionRemaining())
                return Status::failure(FailureCode::CapacityExceeded);
            operation.fresh_ground_roots.push_back(std::make_unique<Sc6ReplayGroundDebrisState::ColdRoot>());
            auto& owner=*operation.fresh_ground_roots.back();
            auto status=operation.target->ground->ConstructColdRoot(i,AdmissionRemaining()+owner.owned_bytes(),owner);
            if(status.ok())status=operation.target->ground->ConstructColdGraph(i,AdmissionRemaining()+owner.owned_bytes(),owner);
            if(status.ok())status=operation.target->ground->ConstructColdPhysics(AdmissionRemaining()+owner.owned_bytes(),owner);
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] ground private root A={} B={} ordinal={} code={} phase={} check={} source={:x} target={:x} flags={:x} ticks={:x}/{:x} detached={} child_observed={:x} B_retained=true published=false\n"),
                operation.target->execution.tick,operation.undo.execution.tick,i,static_cast<unsigned>(status.code),static_cast<unsigned>(owner.phase),
                RC::to_generic_string(owner.check),owner.source,owner.object,owner.flags,owner.primary_tick,owner.secondary_tick,owner.detached,owner.child_observed);
            if(!status.ok()) {
                if(owner.physics_state_offset)RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] ground physical state rejection property_or_guard={:x} child={} before_publication=true\n"),
                    owner.physics_state_offset,owner.child_observed);
                if(owner.body_configuration_offset)RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] ground body configuration rejection offset={:x} observed={:x} before_publication=true\n"),
                    owner.body_configuration_offset,owner.child_observed);
                if(owner.pose_component)RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] ground pose rejection component={:x} offset={:x} captured={:x} actual={:x} before_publication=true\n"),
                    owner.pose_component,owner.pose_offset,owner.pose_expected,owner.pose_actual);
                return status;
            }
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] ground private graph A={} B={} ordinal={} children={} materials={} callback={:x} graph_ready={} published=false physics_created={} initial_body_values_restored=true typed_scene_bindings=false B_retained=true\n"),
                operation.target->execution.tick,operation.undo.execution.tick,i,owner.child_count,owner.material_count,owner.graph_callback,owner.graph_ready,owner.physics_ready);
        }
        operation.fresh_ground_roots_prepared=true;
        // A cold root alone cannot replace historical meshes or bindings.
        // The existing expired-companion/physics admission remains mandatory.
    }
    if(operation.target->traces) {
        if(!operation.undo.traces)return Status::failure(FailureCode::IllegalTransition);
        if(!operation.fresh_trace_sources_ready) {
        if(!operation.fresh_trace_children.empty())return Status::failure(FailureCode::IllegalTransition);
        const auto source_status=operation.target->traces->VisitHistoricalChildSources([&](const auto& source) {
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] historical trace child source A={} B={} state={:x} component={:x} parts={:x} kind={:x} parts_id={} kind_id={} charge={} mesh_asset={:x} animation_class={:x} factory_called=false B_retained=true\n"),
                operation.target->execution.tick,operation.undo.execution.tick,reinterpret_cast<std::uintptr_t>(source.historical.state),
                reinterpret_cast<std::uintptr_t>(source.component),reinterpret_cast<std::uintptr_t>(source.parts),reinterpret_cast<std::uintptr_t>(source.kind),
                source.parts_id,source.kind_id,source.charge,reinterpret_cast<std::uintptr_t>(source.mesh_asset),reinterpret_cast<std::uintptr_t>(source.animation_class));
            if(operation.fresh_trace_children.size()>=16 || sizeof(Sc6ReplayTraceState::FreshChild)+4096>AdmissionRemaining())
                return Status::failure(FailureCode::CapacityExceeded);
            operation.fresh_trace_children.push_back(std::make_unique<Sc6ReplayTraceState::FreshChild>());
            auto& child=*operation.fresh_trace_children.back();
            auto status=operation.target->traces->ConstructFreshChild(source,*operation.undo.traces,AdmissionRemaining(),child);
            if(status.ok())status=operation.target->traces->PrepareFreshChildHistory(source,*operation.undo.traces,child);
            if(status.ok())status=operation.target->traces->PrepareFreshChildAnimation(source,*operation.undo.traces,child,AdmissionRemaining());
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] fresh trace child acquisition A={} B={} code={} phase={} prepared={} state={:x} actor={:x} mesh={:x} B_retained=true published=false\n"),
                operation.target->execution.tick,operation.undo.execution.tick,static_cast<unsigned>(status.code),static_cast<unsigned>(child.factory.phase),
                child.prepared,reinterpret_cast<std::uintptr_t>(child.factory.reference.state),reinterpret_cast<std::uintptr_t>(child.state.actor.object),
                reinterpret_cast<std::uintptr_t>(child.state.mesh.object));
            return status;
        });
        if(!source_status.ok())return source_status;
        operation.fresh_trace_sources_ready=true;
        }
        if(!operation.fresh_trace_children.empty()) {
            std::array<Sc6ReplayTraceState::FreshChild*,16> children{};
            for(std::size_t i=0;i<operation.fresh_trace_children.size();++i)children[i]=operation.fresh_trace_children[i].get();
            using Render=HistoricalRestore::FreshRenderPhase;
            if(operation.fresh_trace_render_phase==Render::Empty) {
                operation.fresh_trace_render_phase=Render::Entered;
                const auto drained=operation.target->traces->DrainFreshChildRenderWork(*operation.undo.traces,
                    {children.data(),operation.fresh_trace_children.size()});
                if(!drained.ok())return drained;
                operation.fresh_trace_render_serial=particle_copy_->retirement_completion().submitted_serial();
                Sc6ReplayParticleCopy::Witness witness{};bool pending{};
                if(!ParticleCopyExperiment(ParticleCopyAction::DrainPrivateOwnerRetirement,&witness,&pending))
                    return Status::failure(FailureCode::PresentationFailed);
                operation.fresh_trace_render_phase=Render::Queued;
                return Status::success();
            }
            if(operation.fresh_trace_render_phase==Render::Queued) {
                const auto& completion=particle_copy_->retirement_completion();
                if(!completion.retired())return Status::success();
                if(completion.result()!=ReplayGpuCompletion::Result::Complete
                    || completion.submitted_serial()<=operation.fresh_trace_render_serial)
                    return Status::failure(FailureCode::PresentationFailed);
                operation.fresh_trace_render_phase=Render::Ready;
                RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] fresh trace constructor render completed children={} native_complete=true GPU_complete=true B_retained=true before_projection=true\n"),
                    operation.fresh_trace_children.size());
            }
            if(operation.fresh_trace_render_phase!=Render::Ready)return Status::failure(FailureCode::IllegalTransition);
            if(operation.trace_projection || sizeof(Sc6ReplayTraceState)>AdmissionRemaining())return Status::failure(FailureCode::CapacityExceeded);
            operation.trace_projection=std::make_unique<Sc6ReplayTraceState>();
            const auto projected=operation.target->traces->ProjectFreshChildren(*operation.undo.traces,
                std::span<Sc6ReplayTraceState::FreshChild* const>(children.data(),operation.fresh_trace_children.size()),
                AdmissionRemaining(),*operation.trace_projection);
            if(!projected.ok())return projected;
            if(operation.trace_survivors || sizeof(Sc6ReplayTraceState)>AdmissionRemaining())return Status::failure(FailureCode::CapacityExceeded);
            operation.trace_survivors=std::make_unique<Sc6ReplayTraceState>();
            const auto survivors=operation.trace_projection->CreateSurvivingOwnerView(
                std::span<Sc6ReplayTraceState::FreshChild* const>(children.data(),operation.fresh_trace_children.size()),
                AdmissionRemaining(),*operation.trace_survivors);
            if(!survivors.ok())return survivors;
        }
    }
    const auto live=operation.target->vfx.ValidateRetainedOwners();
    if(live.ok()) {
        const auto status=TransposeFreshParticleScheduler();if(!status.ok())return status;
        const auto projected=PrepareFreshSchedulerTarget();if(!projected.ok())return projected;
        operation.particle_inventory_ready=true;return Status::success();
    }
    if(live.code==FailureCode::ContextUnavailable || live.code==FailureCode::WrongThread)return live;
    if(!operation.particle_bindings.empty() || !operation.fresh_particles.empty())
        return Status::failure(FailureCode::IllegalTransition);
    const auto factory=[](void* context,const Sc6ReplayVfxState::ReconstructionRequest& request,
        Sc6ReplayVfxState::ReconstructionBinding& binding)->Status {
        auto& host=*static_cast<Sc6ReplayHost*>(context);auto& operation=*host.historical_restore_;
        const auto& target=*operation.target;
        const auto configuration=std::find_if(target.particle_configurations.begin(),target.particle_configurations.end(),
            [&](const auto& row){return row && row->source()==request.source;});
        if(configuration==target.particle_configurations.end() || !(*configuration)->Validate().ok())
            return Status::failure(FailureCode::GenerationMismatch);
        const auto allowance=(*configuration)->reconstruction_allowance();
        const auto metadata=(operation.fresh_particles.size()+1)*sizeof(operation.fresh_particles[0]);
        if(operation.fresh_particles.size()>=32 || metadata+sizeof(HistoricalRestore::FreshParticle)+allowance+4096>host.AdmissionRemaining())
            return Status::failure(FailureCode::CapacityExceeded);
        operation.fresh_particles.reserve(operation.fresh_particles.size()+1);
        operation.fresh_particles.push_back(std::make_unique<HistoricalRestore::FreshParticle>());
        auto& owner=*operation.fresh_particles.back();
        owner.native_allowance=allowance;owner.count=request.emitter_count;
        if(request.gpu_count>request.gpu_ordinals.size() || request.gpu_count>owner.count
            || request.gpu_count*sizeof(HistoricalRestore::FreshParticle::Gpu)>host.AdmissionRemaining())
            return Status::failure(FailureCode::CapacityExceeded);
        owner.gpus.resize(request.gpu_count);
        for(std::size_t i=0;i<owner.gpus.size();++i) {
            if(request.gpu_ordinals[i]>=owner.count)return Status::failure(FailureCode::GenerationMismatch);
            for(std::size_t j=0;j<i;++j)if(request.gpu_ordinals[j]==request.gpu_ordinals[i])return Status::failure(FailureCode::GenerationMismatch);
            owner.gpus[i].ordinal=request.gpu_ordinals[i];
        }
        // Publish a stable owner before every native allocation/registration.
        // A's source UObject is never visited, even if its address is reused.
        auto status=(*configuration)->Clone(owner.lease,owner.cold);
        if(!status.ok())return status;
        using Native=Sc6ReplayParticleComponentOwner;
        if(!Native::RegisterInactive(host.image_base_,host.world_,owner.lease,owner.cold,owner.registration)
            || !Native::RemoveInactiveRenderState(host.image_base_,owner.lease,owner.registration,owner.render))
            return Status::failure(FailureCode::RestorePreflightFailed);
        if(owner.gpus.empty()) {
            Sc6ReplayCpuEmitterState::FreshGpuOwner empty;
            status=target.vfx.ConstructFreshParticleOwner(request.source,owner.cold.copy,host.AdmissionRemaining(),owner.binding,empty);
            if(!status.ok())return status;
        }
        for(std::size_t i=0;i<owner.gpus.size();++i) {
            auto& gpu=owner.gpus[i];
            status=target.vfx.ConstructFreshParticleOwner(request.source,owner.cold.copy,host.AdmissionRemaining(),owner.binding,gpu.owner,gpu.ordinal);
            if(!status.ok()) {
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] fresh GPU construction rejected source={:x} target={:x} ordinal={} code={} root={:x} render={:x} captured_render={:x} verification_mask={:x} phase={} B_retained=true\n"),
                    request.source,owner.cold.copy,gpu.ordinal,static_cast<unsigned>(status.code),reinterpret_cast<std::uintptr_t>(gpu.owner.root),
                    gpu.owner.render,gpu.owner.captured_render,gpu.owner.verification_failure,static_cast<unsigned>(gpu.owner.phase));
                return status;
            }
            for(std::size_t j=0;j<i;++j)if(owner.gpus[j].owner.root==gpu.owner.root || owner.gpus[j].owner.render==gpu.owner.render)
                return Status::failure(FailureCode::GenerationMismatch);
        }
        if(!owner.count || owner.count>128
            || Native::Read<std::uintptr_t>(owner.cold.copy,0xa50)
            || Native::Read<int>(owner.cold.copy,0xa58) || Native::Read<int>(owner.cold.copy,0xa5c))
            return Status::failure(FailureCode::UnsupportedContent);
        const auto requested=owner.count*sizeof(void*);
        owner.slot_bytes=reinterpret_cast<std::size_t(*)(std::size_t,unsigned)>(host.image_base_+0xd50dc0)(requested,0);
        if(owner.slot_bytes<requested || owner.slot_bytes>host.AdmissionRemaining())return Status::failure(FailureCode::CapacityExceeded);
        owner.slots=reinterpret_cast<void*(*)(std::size_t)>(host.image_base_+0x4a61c0)(requested);
        if(!owner.slots)return Status::failure(FailureCode::CapacityExceeded);
        std::memset(owner.slots,0,requested);
        for(const auto& gpu:owner.gpus)static_cast<void**>(owner.slots)[gpu.ordinal]=gpu.owner.root;
        std::array<std::byte,16> header{};
        const auto count=static_cast<int>(owner.count);
        std::memcpy(header.data(),&owner.slots,8);std::memcpy(header.data()+8,&count,4);std::memcpy(header.data()+12,&count,4);
        owner.slots_installed=true;
        if(!Native::SetPrivateEmitterHeader(owner.cold.copy,header))return Status::failure(FailureCode::RestoreWriteFailed);
        binding={owner.binding,&owner.lease};return Status::success();
    };
    Sc6ReplayVfxState::ReconstructionFailure failure{};
    auto status=operation.target->vfx.PrepareReconstructionBindings(operation.undo.vfx,AdmissionRemaining(),operation.particle_bindings,{this,factory},&failure);
    if(!status.ok()) {
        operation.target->vfx.DiagnoseReconstructionOwnerFailure(&operation,[](const void* context,const Sc6ReplayObjectLease::FailureWitness& failure) {
            const auto& operation=*static_cast<const HistoricalRestore*>(context);
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] particle reconstruction dependency A={} B={} ordinal={} object={:x} index={} serial={} reference={} invalidated={} collector_changed={} membership={} vtable={:x} slots={} slot_kind={} slot_id={} attachments={} materials={} material_parents={} completions={} providers={} read_only=true\n"),
                operation.target->execution.tick,operation.undo.execution.tick,failure.ordinal,reinterpret_cast<std::uintptr_t>(failure.original),failure.index,failure.serial,
                failure.reference,failure.invalidated,failure.collector_changed,failure.membership,failure.vtable,
                failure.captured_manager_slots,failure.captured_slot_kind,failure.captured_slot_id,failure.captured_attachment_users,
                failure.captured_material_users,failure.captured_material_parent_users,failure.captured_completion_users,failure.captured_provider_users);
            const auto object=reinterpret_cast<std::uintptr_t>(failure.original);
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] reconstruction companion captured identity object={:x} name={:x} class={:x} captured={} read_only=true\n"),
                object,failure.object_name,failure.class_name,failure.captured_identity);
            bool ground_pointer_match=false,ground_indexed_match=false;
            unsigned ground_mesh_ordinal=0xffffffffu;
            if(operation.target->ground) {
                const auto meshes=operation.target->ground->meshes();
                for(unsigned i=0;i<meshes.size();++i)if(meshes[i].component==object) {
                    ground_pointer_match=true;
                    if(meshes[i].weak[0]==failure.index && meshes[i].weak[1]==failure.serial) {
                        ground_indexed_match=true;ground_mesh_ordinal=i;break;
                    }
                }
            }
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] reconstruction companion ground snapshot={} pointer_match={} indexed_match={} mesh_ordinal={} read_only=true\n"),
                bool(operation.target->ground),ground_pointer_match,ground_indexed_match,ground_mesh_ordinal);
            const auto& physics=operation.target->physics;
            for(unsigned scene=0;scene<physics.scenes.size();++scene)
                for(unsigned i=0;i<physics.observed_actors[scene] && i<physics.actors[scene].size();++i) {
                    const auto& actor=physics.actors[scene][i];
                    if(actor.component==object) {
                        std::uint8_t body_flags{};
                        if(actor.kind==2)std::memcpy(&body_flags,actor.properties.data()+0x9c,sizeof(body_flags));
                        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] reconstruction companion physics scene={} actor_index={} component={:x} actor={:x} body={:x} kind={} shapes={} component_index={} component_serial={} body_flags={:x} kinematic={} read_only=true\n"),
                            scene,i,actor.component,actor.actor,actor.body,actor.kind,actor.query_handle_count,
                            actor.component_weak[0],actor.component_weak[1],body_flags,bool(body_flags&1));
                    }
                }
            for(const auto& row:operation.target->world.stage_visibility()) {
                if(row.actor==object || row.root==object || row.effect==object)
                    RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] reconstruction companion stage actor={:x} root={:x} effect={:x} read_only=true\n"),row.actor,row.root,row.effect);
                for(const auto& component:row.components)if(component.object==object)
                    RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] reconstruction companion stage component={:x} asset={:x} read_only=true\n"),component.object,component.asset);
            }
        });
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] particle reconstruction inventory rejected target={} B={} code={} source={:x} roots={} gpu={} managed={} slots={} lux={} source_live={} before_factory={}\n"),
            operation.target->execution.tick,operation.undo.execution.tick,static_cast<unsigned>(status.code),failure.source,
            failure.roots,failure.gpu,failure.managed,failure.slots,failure.lux,failure.source_live,operation.fresh_particles.empty());
        return status;
    }
    if(!operation.fresh_ground_roots.empty())return Status::failure(FailureCode::UnsupportedContent);
    status=TransposeFreshParticleScheduler();
    if(!status.ok()) {
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] fresh scheduler transposition rejected code={} staged={} published={} committed={} B_retained=true\n"),
            static_cast<unsigned>(status.code),operation.fresh_scheduler_staged,operation.fresh_scheduler_published,operation.fresh_scheduler_committed);
        return status;
    }
    status=PrepareFreshSchedulerTarget();if(!status.ok())return status;
    operation.particle_inventory_ready=true;
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] particle reconstruction prepared target={} B={} fresh={} complete_B_unchanged=true GPU_unpublished=true\n"),
        operation.target->execution.tick,operation.undo.execution.tick,operation.fresh_particles.size());
    return Status::success();
}

Status Sc6ReplayHost::PrepareFreshSchedulerTarget()
{
    auto& operation=*historical_restore_;
    const auto count=operation.particle_bindings.size()+operation.fresh_trace_children.size();
    if(!count)return Status::success();
    if(!operation.scheduler_bindings.empty() || !operation.fresh_scheduler_ready || count>128
        || count*sizeof(Sc6ReplaySchedulerState::ComponentBinding)>AdmissionRemaining())
        return Status::failure(FailureCode::CapacityExceeded);
    operation.scheduler_bindings.reserve(count);
    if(operation.scheduler_bindings.capacity()*sizeof(Sc6ReplaySchedulerState::ComponentBinding)>AdmissionRemaining())
        return Status::failure(FailureCode::CapacityExceeded);
    for(const auto& binding:operation.particle_bindings)operation.scheduler_bindings.push_back(binding);
    for(const auto& child:operation.fresh_trace_children) {
        if(!child || !operation.target->traces)return Status::failure(FailureCode::GenerationMismatch);
        Sc6ReplaySchedulerState::ComponentBinding binding;
        const auto status=operation.target->traces->FreshChildMeshBinding(*child,binding);if(!status.ok())return status;
        operation.scheduler_bindings.push_back(binding);
    }
    const Sc6ReplaySchedulerState::ComponentOwnerProof proof{&operation,
        [](const void* context,std::span<const Sc6ReplaySchedulerState::ComponentBinding> rows) {
            const auto& owner=*static_cast<const HistoricalRestore*>(context);
            if(rows.size()!=owner.particle_bindings.size()+owner.fresh_trace_children.size())
                return Status::failure(FailureCode::GenerationMismatch);
            const auto same=[](const auto& a,const auto& b) {
                return a.identity.source==b.identity.source && a.identity.target==b.identity.target
                    && a.identity.source_weak==b.identity.source_weak && a.identity.target_weak==b.identity.target_weak && a.lease==b.lease;
            };
            if(!owner.particle_bindings.empty()) {
                const auto status=owner.target->vfx.ValidateReconstructionBindings(owner.particle_bindings);if(!status.ok())return status;
            }
            std::size_t i{};
            for(const auto& binding:owner.particle_bindings)if(!same(rows[i++],binding))return Status::failure(FailureCode::GenerationMismatch);
            for(const auto& child:owner.fresh_trace_children) {
                Sc6ReplaySchedulerState::ComponentBinding binding;
                if(!child || !owner.target->traces)return Status::failure(FailureCode::GenerationMismatch);
                const auto status=owner.target->traces->FreshChildMeshBinding(*child,binding);if(!status.ok())return status;
                if(!same(rows[i++],binding))return Status::failure(FailureCode::GenerationMismatch);
            }
            return Status::success();
        }};
    Sc6ReplaySchedulerState::BindingFailure scheduler_failure{};
    const auto status=operation.target->scheduler.ReconstructOwnedComponentBindings(proof,operation.scheduler_bindings,
        AdmissionRemaining(),operation.fresh_scheduler_target,&scheduler_failure);
    if(!status.ok()) {
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] fresh scheduler projection rejected code={} check={} tick={:x} owner={:x} weak={}/{} offset={:x} expected={:x} actual={:x} B_retained=true\n"),
            static_cast<unsigned>(status.code),RC::to_generic_string(scheduler_failure.check?scheduler_failure.check:"unknown"),
            scheduler_failure.tick,scheduler_failure.owner,scheduler_failure.weak[0],scheduler_failure.weak[1],scheduler_failure.offset,scheduler_failure.expected,scheduler_failure.actual);
        if(operation.target->traces && operation.undo.traces)
            operation.target->traces->DescribeOwnershipDelta(*operation.undo.traces);
        return status;
    }
    return status;
}

Status Sc6ReplayHost::TransposeFreshParticleScheduler()
{
    auto& operation=*historical_restore_;
    if((operation.fresh_particles.empty() && operation.fresh_trace_children.empty()) || operation.fresh_scheduler_ready)return Status::success();
    const Sc6ReplaySchedulerState::RetainedPrimaryTicks fresh{&operation,
        [](const void* context,void* visitor,bool(*accept)(void*,std::uintptr_t)) {
            const auto& operation=*static_cast<const HistoricalRestore*>(context);
            for(const auto& owner:operation.fresh_trace_children) {
                if(owner && owner->factory.phase==Sc6ReplayTraceChildFactory::Phase::Empty)continue;
                if(!owner || !owner->prepared || !owner->lease.Validate().ok()
                    || !accept(visitor,reinterpret_cast<std::uintptr_t>(owner->state.mesh.object)))
                    return Status::failure(FailureCode::GenerationMismatch);
            }
            for(const auto& owner:operation.fresh_particles) {
                if(owner && owner->registration.phase==Sc6ReplayParticleComponentOwner::Phase::Empty)continue;
                if(!owner || owner->registration.phase!=Sc6ReplayParticleComponentOwner::Phase::Registered
                    || !owner->lease.ValidateObject(reinterpret_cast<void*>(owner->cold.copy)).ok()
                    || !accept(visitor,owner->cold.copy))return Status::failure(FailureCode::GenerationMismatch);
            }
            return Status::success();
        }};
    auto& b=operation.undo.scheduler;
    auto status=Status::success();
    if(!operation.fresh_scheduler_staged) {
        status=operation.fresh_scheduler_staging.Capture(image_base_,world_,AdmissionRemaining(),fresh,&b);
        if(!status.ok())return status;
        operation.fresh_scheduler_staged=true;
    }
    if(!operation.fresh_scheduler_published) {
        Sc6ReplaySchedulerState::PreflightFailure failure{};
        status=b.PrepareRestore(operation.fresh_scheduler_staging,image_base_,world_,AdmissionRemaining(),
            operation.fresh_scheduler_transfer,&failure,nullptr,fresh);
        if(!status.ok())return status;
        // Only physical storage changes. Publish B's exact logical schedule,
        // removing private fresh ticks before preparing the mapped A schedule.
        operation.fresh_scheduler_published=true;
        status=b.PublishBacking(operation.fresh_scheduler_staging,image_base_,world_,
            AdmissionRemaining()+operation.fresh_scheduler_transfer.owned_bytes(),operation.fresh_scheduler_transfer);
        if(!status.ok())return status;
    }
    if(!operation.fresh_scheduler_committed) {
        status=b.CommitBacking(image_base_,world_,operation.fresh_scheduler_transfer);
        if(!status.ok())return status;
        operation.fresh_scheduler_committed=true;
    }
    status=operation.fresh_scheduler_adopted.Capture(image_base_,world_,AdmissionRemaining(),{},&b);
    if(!status.ok())return status;
    if(!b.SameLogicalImage(operation.fresh_scheduler_adopted))return Status::failure(FailureCode::RestoreVerificationFailed);
    b=std::move(operation.fresh_scheduler_adopted);
    operation.fresh_scheduler_staging={};
    operation.fresh_scheduler_ready=true;
    return Status::success();
}

Status Sc6ReplayHost::RetirePreparedFreshParticles(bool& pending)
{
    pending=false;auto& operation=*historical_restore_;
    if(operation.fresh_particles.empty() && operation.fresh_trace_children.empty() && operation.fresh_ground_roots.empty())return Status::success();
    if(operation.manager.published() || operation.gpu.published() || operation.cpu.published()
        || operation.scheduler.published() || !particle_copy_ || particle_copy_->witness().native_write_uncommitted)
        return Status::failure(FailureCode::IllegalTransition);
    auto status=TransposeFreshParticleScheduler();
    if(!status.ok())return status;
    using Native=Sc6ReplayParticleComponentOwner;
    using GpuPhase=Sc6ReplayCpuEmitterState::FreshGpuOwner::Phase;
    bool queued=false;
    for(auto& pointer:operation.fresh_ground_roots) {
        auto& owner=*pointer;const auto before=owner.phase;
        status=Sc6ReplayGroundDebrisState::DestroyColdRoot(owner,particle_copy_->retirement_completion().submitted_serial());
        if(!status.ok())return status;
        queued|=before==Sc6ReplayGroundDebrisState::ColdRoot::Phase::Cold;
    }
    for(auto& pointer:operation.fresh_trace_children) {
        auto& child=*pointer;if(child.retired)continue;
        if(child.factory.phase==Sc6ReplayTraceChildFactory::Phase::Empty) {
            child.allowance=0;child.retired=true;continue;
        }
        if(!child.prepared || !operation.undo.traces)return Status::failure(FailureCode::UndoFailed);
        if(child.retirement.step==Sc6ReplayTraceState::Retirement::Step::Finished)continue;
        child.retirement_serial=particle_copy_->retirement_completion().submitted_serial();
        status=operation.undo.traces->RetireFreshChild(child);if(!status.ok())return status;
        queued=true;
    }
    for(auto& pointer:operation.fresh_particles) {
        auto& owner=*pointer;if(owner.retired)continue;
        if(owner.slots_installed) {
            // One bounded record on the rejecting path. In particular, an
            // invalid lease must not read a header, and a mismatched header
            // must not dereference the retained private slot allocation.
            const auto reject=[&](const char* predicate,std::uint64_t observed,std::uint64_t expected,std::size_t ordinal) {
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] private particle retirement rejected predicate={} observed={} expected={} ordinal={} component={:x} slots={:x} count={} slot_bytes={} gpu_count={} settled={} native_dead={} retired={} B_retained=true\n"),
                    RC::to_generic_string(predicate),observed,expected,ordinal,owner.cold.copy,
                    reinterpret_cast<std::uintptr_t>(owner.slots),owner.count,owner.slot_bytes,owner.gpus.size(),owner.settled,owner.native_dead,owner.retired);
                return Status::failure(FailureCode::GenerationMismatch);
            };
            const auto lease=owner.lease.ValidateObject(reinterpret_cast<void*>(owner.cold.copy));
            if(!lease.ok())return reject("lease",static_cast<unsigned>(lease.code),0,~std::size_t{});
            const auto actual_slots=Native::Read<void*>(owner.cold.copy,0xa50);
            if(actual_slots!=owner.slots)return reject("header_slots",reinterpret_cast<std::uintptr_t>(actual_slots),reinterpret_cast<std::uintptr_t>(owner.slots),~std::size_t{});
            const auto actual_count=Native::Read<int>(owner.cold.copy,0xa58);
            if(actual_count!=owner.count)return reject("header_count",static_cast<std::uint64_t>(actual_count),owner.count,~std::size_t{});
            const auto actual_capacity=Native::Read<int>(owner.cold.copy,0xa5c);
            if(actual_capacity!=owner.count)return reject("header_capacity",static_cast<std::uint64_t>(actual_capacity),owner.count,~std::size_t{});
            for(std::size_t i=0;i<owner.count;++i) {
                void* expected{};
                for(const auto& gpu:owner.gpus)if(gpu.ordinal==i)expected=gpu.owner.root;
                const auto actual=static_cast<void**>(owner.slots)[i];
                if(actual!=expected)return reject("slot_root",reinterpret_cast<std::uintptr_t>(actual),reinterpret_cast<std::uintptr_t>(expected),i);
            }
            if(!Native::SetPrivateEmitterHeader(owner.cold.copy,{}))return Status::failure(FailureCode::UndoFailed);
            owner.slots_installed=false;
        }
        if(owner.slots) {reinterpret_cast<void(*)(void*)>(image_base_+0xd46a00)(owner.slots);owner.slots=nullptr;owner.slot_bytes=0;}
        for(auto& gpu:owner.gpus) {
            if(gpu.owner.phase==GpuPhase::Constructed) {
                gpu.retirement_serial=particle_copy_->retirement_completion().submitted_serial();
                status=operation.target->vfx.QueueFreshGpuRetirement(owner.binding,gpu.owner,gpu.ordinal);
                if(!status.ok())return status;
                queued=true;
            } else if(gpu.owner.phase!=GpuPhase::Empty && gpu.owner.phase!=GpuPhase::RetirementQueued)
                return Status::failure(FailureCode::IllegalTransition);
        }
    }
    if(queued) {
        Sc6ReplayParticleCopy::Witness witness{};
        if(!ParticleCopyExperiment(ParticleCopyAction::DrainPrivateOwnerRetirement,&witness,&pending))return Status::failure(FailureCode::PresentationFailed);
        pending=true;return Status::success();
    }
    for(auto& pointer:operation.fresh_ground_roots) {
        auto& owner=*pointer;
        if(owner.phase==Sc6ReplayGroundDebrisState::ColdRoot::Phase::Released)continue;
        const auto& completion=particle_copy_->retirement_completion();
        if(!completion.retired() || completion.submitted_serial()<=owner.retirement_serial) {pending=true;return Status::success();}
        status=Sc6ReplayGroundDebrisState::ReleaseColdRoot(owner,completion);if(!status.ok())return status;
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] ground private root retired native_complete=true GPU_complete=true B_retained=true\n"));
    }
    for(auto& pointer:operation.fresh_trace_children) {
        auto& child=*pointer;if(child.retired)continue;
        if(child.retirement.step!=Sc6ReplayTraceState::Retirement::Step::Finished)return Status::failure(FailureCode::UndoFailed);
        if(!particle_copy_->retirement_completion().retired()
            || particle_copy_->retirement_completion().submitted_serial()<=child.retirement_serial) {pending=true;return Status::success();}
        if(particle_copy_->retirement_completion().result()!=ReplayGpuCompletion::Result::Complete)
            return Status::failure(FailureCode::PresentationFailed);
        status=operation.undo.traces->ReleaseFreshChildOwnership(child);if(!status.ok())return status;
        child.allowance=0;child.retired=true;
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] fresh trace child preparation retired native_complete=true GPU_complete=true B_retained=true\n"));
    }
    for(auto& pointer:operation.fresh_particles) {
        auto& owner=*pointer;if(owner.retired)continue;
        for(const auto& gpu:owner.gpus)
            if(gpu.owner.phase==GpuPhase::RetirementQueued && (!particle_copy_->retirement_completion().retired()
                || particle_copy_->retirement_completion().submitted_serial()<=gpu.retirement_serial)) {pending=true;return Status::success();}
        if(owner.registration.phase!=Native::Phase::Empty) {
            if(!Native::RetireInactive(image_base_,owner.lease,owner.cold,owner.registration)) {
                const auto component=owner.cold.copy;
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] private particle retirement rejected component={:x} check={} phase={} flags={:x} proxy={:x} render_refs={} slots={:x} count={} capacity={} tick_function={:x} async_task={:x} async_flags={}/{} read_only=true B_retained=true\n"),
                    component,RC::to_generic_string(owner.registration.check),static_cast<unsigned>(owner.registration.phase),
                    Native::Read<unsigned>(component,0x188),Native::Read<std::uintptr_t>(component,0x790),Native::Read<int>(component,0x7a0),
                    Native::Read<std::uintptr_t>(component,0xa50),Native::Read<int>(component,0xa58),Native::Read<int>(component,0xa5c),
                    Native::Read<std::uintptr_t>(component,0x1d0),Native::Read<std::uintptr_t>(component,0xa70),
                    Native::Read<unsigned char>(component,0xa80),Native::Read<unsigned char>(component,0xa81));
                return Status::failure(FailureCode::UndoFailed);
            }
        } else if(owner.cold.created) {
            if(!Native::Retire(image_base_,owner.lease,owner.cold))return Status::failure(FailureCode::UndoFailed);
        } else if(owner.cold.native_construction_entered || !owner.lease.Release().ok())return Status::failure(FailureCode::UndoFailed);
        owner.retired=true;
    }
    return Status::success();
}


Status Sc6ReplayHost::BeginFreshParticleRenderOwners()
{
    auto& operation=*historical_restore_;
    if(operation.fresh_particles.empty() && !particle_copy_->trace_render_pending())return Status::success();
    if(operation.fresh_render_phase!=HistoricalRestore::FreshRenderPhase::Empty
        || !operation.execution || !particle_copy_->execution_started()
        || particle_copy_->witness().pending || particle_command_pending_.load())
        return Status::failure(FailureCode::IllegalTransition);
    operation.fresh_render_phase=HistoricalRestore::FreshRenderPhase::Entered;
    if(particle_copy_->trace_render_pending()) {
        auto status=operation.TargetTraces().ReconstructVisibleRenderingForUndo(operation.execution->trace_target_reconstruction,false);
        if(!status.ok())return status;
    }
    using Native=Sc6ReplayParticleComponentOwner;
    for(auto& pointer:operation.fresh_particles) {
        auto& owner=*pointer;
        if(!Native::CreateActiveRenderState(image_base_,owner.lease,owner.registration,owner.render,AdmissionRemaining()))
            return Status::failure(FailureCode::RestoreVerificationFailed);
        for(auto& gpu:owner.gpus)
            if(!Native::QueueGpuRenderRegistration(image_base_,owner.lease,owner.registration,owner.render,
                reinterpret_cast<std::uintptr_t>(gpu.owner.root),gpu.owner.render,gpu.registration))
                return Status::failure(FailureCode::RestoreVerificationFailed);
    }
    Sc6ReplayParticleCopy::Witness witness{};bool pending{};
    if(!ParticleCopyExperiment(ParticleCopyAction::CompleteParticleRenderOwners,&witness,&pending))
        return Status::failure(FailureCode::PresentationFailed);
    operation.fresh_render_phase=HistoricalRestore::FreshRenderPhase::Queued;
    operation.witness.pending=true;
    return Status::success();
}

Status Sc6ReplayHost::SettleFreshParticleOwners()
{
    auto& operation=*historical_restore_;
    if(operation.fresh_particles.empty())return Status::success();
    if(!operation.execution)return Status::failure(FailureCode::IllegalTransition);
    for(std::size_t i=0;i<operation.gpu.size();++i)
        if(!operation.gpu.replacement(i) || !operation.gpu.replacement(i)->execution_settled())
            return Status::failure(FailureCode::IllegalTransition);
    for(std::size_t i=0;i<operation.cpu.size();++i)
        if(!operation.cpu.replacement(i) || !operation.cpu.replacement(i)->execution_settled())
            return Status::failure(FailureCode::IllegalTransition);
    for(auto& pointer:operation.fresh_particles) {
        auto& owner=*pointer;
        const auto component=reinterpret_cast<void*>(owner.binding.target);
        const auto gpu=operation.gpu.FreshComponentRoots(owner.binding.target),cpu=operation.cpu.FreshComponentRoots(owner.binding.target);
        const bool dead=(gpu || cpu)
            && (!gpu || operation.gpu.FreshComponentRetired(owner.binding.target,owner.binding.target_weak))
            && (!cpu || operation.cpu.FreshComponentRetired(owner.binding.target,owner.binding.target_weak));
        if(!dead && (!operation.execution->current.RetainsComponent(component)
            || !owner.lease.ValidateObject(component).ok()))return Status::failure(FailureCode::GenerationMismatch);
        if(dead && operation.execution->current.RetainsComponent(component))return Status::failure(FailureCode::GenerationMismatch);
        owner.native_dead=dead;owner.settled=true;
    }
    return Status::success();
}

Status Sc6ReplayHost::ReleaseFreshParticleOwnership(bool commit)
{
    auto& operation=*historical_restore_;
    if(std::any_of(operation.fresh_trace_children.begin(),operation.fresh_trace_children.end(),[](const auto& child) {return !child;}))
        return Status::failure(FailureCode::IllegalTransition);
    const auto trace_pending=std::any_of(operation.fresh_trace_children.begin(),operation.fresh_trace_children.end(),[](const auto& child) {
        return !child->retired && !child->adopted;
    });
    if(operation.fresh_particles.empty() && !trace_pending)return Status::success();
    if(!operation.execution || operation.gpu.published() || operation.cpu.published() || operation.manager.published()
        || particle_command_pending_.load() || particle_copy_->witness().pending
        || particle_copy_->witness().phase!=Sc6ReplayParticleCopy::Phase::Released
        || (commit && !operation.native_retired))return Status::failure(FailureCode::IllegalTransition);
    if(trace_pending && !operation.undo.traces)return Status::failure(FailureCode::IllegalTransition);
    if(trace_pending && commit && operation.execution->capture!=HistoricalRestore::Execution::Capture::Current)
        return Status::failure(FailureCode::IllegalTransition);
    // GPU/CPU settlement exported these receipts while all prepared owners
    // still existed. Commit/Undo may have cleared those participant journals.
    for(const auto& pointer:operation.fresh_particles) {
        const auto& owner=*pointer;if(owner.retired || owner.adopted)continue;
        if(!owner.settled || (!commit && !owner.native_dead))return Status::failure(FailureCode::IllegalTransition);
        if(!owner.native_dead && !owner.lease.ValidateObject(reinterpret_cast<void*>(owner.binding.target)).ok())
            return Status::failure(FailureCode::GenerationMismatch);
    }
    for(auto& pointer:operation.fresh_trace_children) {
        auto& child=*pointer;if(child.retired || child.adopted)continue;
        // Execution owns the captured C image through final release. B must
        // remain unchanged and cannot contain reconstructed fresh identities.
        // Recovery permits only native-dead children and retains B's proof.
        const auto& ownership=commit?operation.execution->traces:*operation.undo.traces;
        const auto status=ownership.ReleaseExecutedFreshChildOwnership(child,commit);
        if(!status.ok())return status;
    }
    for(auto& pointer:operation.fresh_particles) {
        auto& owner=*pointer;if(owner.retired || owner.adopted)continue;
        const auto status=owner.lease.Release();if(!status.ok())return status;
        // The native component owns surviving C storage; native teardown owns
        // dead storage. Neither path manually frees a root, slot array or proxy.
        owner.slots=nullptr;owner.slot_bytes=0;owner.slots_installed=false;
        owner.gpus.clear();owner.native_allowance=0;owner.render.charged_bytes=0;
        owner.retired=owner.native_dead;owner.adopted=!owner.native_dead;
    }
    return Status::success();
}
