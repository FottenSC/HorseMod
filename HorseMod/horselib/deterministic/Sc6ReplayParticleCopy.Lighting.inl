#define LIGHTING_REJECT() do { RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] lighting rejected line={} dirty={} transferred={}\n"),__LINE__,lighting_dirty_,lighting_transferred_); return false; } while(false)
// Included inside Horse::Deterministic at the existing held RT boundary.
// Native cache+50 owns block records; +140 owns point allocations keyed by
// component ID. A uses private native storage; displaced B remains complete
// until cancellation closes. No historical native allocation is dereferenced
// merely because its old address was captured.
bool Sc6ReplayParticleCopy::PrepareTraceRenderOwner(const Sc6ReplayTraceState& original) noexcept
{
    if(lighting_dirty_ || witness_.target_prepared || (trace_render_owner_ && trace_render_owner_!=&original)
        || !original.ValidateValues().ok())return false;
    trace_render_owner_=&original;
    return true;
}
bool Sc6ReplayParticleCopy::TraceRenderBinding(const LightingPrimitive& row,bool dormant) const noexcept
{
    if(!lighting_executing_ || !trace_render_owner_ || row.proxy_type!=base_+0x39af350 || row.slot_count!=1)return false;
    if(FreshTraceRenderBinding(row))return !dormant || (!(Field<unsigned>(row.component,0x240)&0x10)
        && !Field<std::uintptr_t>(row.component,0x790) && !Field<std::uintptr_t>(row.component,0xa18));
    return trace_render_owner_->RetainedRenderBinding(row.component,row.weak,row.id,row.proxy_inputs[0],dormant);
}

bool Sc6ReplayParticleCopy::PrepareFreshTraceRenderOwners(const TraceRenderOwnerProof& owners,std::size_t budget) noexcept
{
    if(!owners.count)return true;
    const auto reject=[&](unsigned check,std::size_t index=0) {
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] fresh trace render preparation rejected check={} child={} phase={} owners={} primitives={} uniforms={} B_retained=true\n"),
            check,index,static_cast<unsigned>(witness_.phase),owners.count,lighting_a_.primitives.size(),lighting_a_.uniforms.size());
        return false;
    };
    if(witness_.phase!=Phase::UndoReady || !witness_.undo_ready || !trace_render_owner_ || !completion_.retired()
        || witness_.target_prepared || lighting_dirty_ || lighting_executing_ || lighting_transferred_
        || !lighting_a_.captured || !lighting_b_.captured || fresh_trace_render_count_
        || !owners.context || !owners.binding || owners.count>fresh_trace_render_owners_.size())return reject(1);
    std::array<FreshTraceRenderOwner,16> plans{};
    std::array<ReplayLightingBinding::PrimitiveIdReplacement,16> keys{};
    __try {
        for(std::size_t i=0;i<owners.count;++i) {
            auto& plan=plans[i];
            if(!owners.binding(owners.context,i,plan.binding,true).ok())return reject(2,i);
            const auto& id=plan.binding.identity;
            plan.target_id=Field<unsigned>(id.target,0x420);plan.mesh=Field<std::uintptr_t>(id.target,0x910);
            const auto row=std::find_if(lighting_a_.primitives.begin(),lighting_a_.primitives.end(),[&](const auto& p) {
                return p.component==id.source && p.weak==id.source_weak;
            });
            if(row==lighting_a_.primitives.end()) {
                std::uintptr_t source_mesh{};
                if(!owners.dormant || !owners.dormant(owners.context,i,plan.source_id,source_mesh)
                    || source_mesh!=plan.mesh
                    || std::any_of(lighting_a_.primitives.begin(),lighting_a_.primitives.end(),[&](const auto& p){return p.component==id.source;})
                    || std::any_of(lighting_a_.uniforms.begin(),lighting_a_.uniforms.end(),[&](const auto& u){return u.component==id.source;}))return reject(3,i);
            } else {
            if(!row->id || !plan.target_id || plan.target_id==UINT_MAX
                || row->id==plan.target_id || row->slot_count!=1 || row->proxy_type!=base_+0x39af350
                || row->proxy_inputs[0]!=plan.mesh || !plan.mesh
                || std::count_if(lighting_a_.primitives.begin(),lighting_a_.primitives.end(),[&](const auto& p) {return p.component==id.source;})!=1
                || std::count_if(lighting_a_.uniforms.begin(),lighting_a_.uniforms.end(),[&](const auto& u) {return u.component==id.source && u.ordinal==0;})!=1)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] fresh trace render shape child={} source_id={} target_id={} slots={} proxy_type={:x} source_mesh={:x} native_mesh={:x} source_rows={} uniforms={}\n"),
                    i,row->id,plan.target_id,row->slot_count,row->proxy_type,row->proxy_inputs[0],plan.mesh,
                    std::count_if(lighting_a_.primitives.begin(),lighting_a_.primitives.end(),[&](const auto& p){return p.component==id.source;}),
                    std::count_if(lighting_a_.uniforms.begin(),lighting_a_.uniforms.end(),[&](const auto& u){return u.component==id.source && u.ordinal==0;}));
                return reject(4,i);
            }
            plan.source_id=row->id;
            }
            if(!plan.source_id || plan.source_id==UINT_MAX || !plan.target_id || plan.target_id==UINT_MAX
                || plan.source_id==plan.target_id || !plan.mesh)return reject(4,i);
            for(const auto& u:lighting_a_.uniforms)if(u.component==id.source && u.ordinal!=0)return reject(5,i);
            for(const auto& b:lighting_b_.primitives)if(b.component==id.target || b.id==plan.target_id)return reject(6,i);
            for(const auto& a:lighting_a_.primitives)if(a.id==plan.target_id
                || (a.component==id.target && a.weak==id.target_weak))return reject(7,i);
            for(std::size_t j=0;j<i;++j)if(plans[j].binding.identity.source==id.source
                || plans[j].binding.identity.target==id.target || plans[j].target_id==plan.target_id)return reject(8,i);
            keys[i]={plan.source_id,plan.target_id};
        }
        if(witness_.bytes>budget || sizeof(plans)>budget-witness_.bytes)return reject(9);
        auto& map=lighting_a_.maps[1];unsigned rows{};std::memcpy(&rows,map.header.data()+8,4);
        if(!ReplayLightingBinding::RebindPrimitiveAllocationKeys(map.entries,rows,map.flags,map.hashes,std::span(keys).first(owners.count)))return reject(10);
        std::uintptr_t heap{};std::memcpy(&heap,map.header.data()+0x40,8);
        if(!heap)std::memcpy(map.header.data()+0x38,map.hashes.data(),4);
        // Project each original entry once. A destination may reuse another
        // expired source address; sequential substitutions would remap it twice.
        for(auto& row:lighting_a_.primitives)for(std::size_t i=0;i<owners.count;++i) {
            const auto& plan=plans[i];const auto& id=plan.binding.identity;
            if(row.component==id.source && row.weak==id.source_weak) {
                row.component=id.target;row.weak=id.target_weak;row.id=plan.target_id;
                row.address=row.proxy=0;row.slots={};row.binding=LightingPrimitive::Binding::PendingTraceTarget;
                break;
            }
        }
        for(auto& u:lighting_a_.uniforms)for(std::size_t i=0;i<owners.count;++i) {
            const auto& id=plans[i].binding.identity;
            if(u.component==id.source) {u.component=id.target;u.slot=0;break;}
        }
        for(auto& owner:lighting_a_.owners)for(std::size_t i=0;i<owners.count;++i) {
            const auto& id=plans[i].binding.identity;
            if(owner==reinterpret_cast<void*>(id.source)) {owner=reinterpret_cast<void*>(id.target);break;}
        }
        fresh_trace_render_proof_=owners;fresh_trace_render_owners_=plans;fresh_trace_render_count_=owners.count;
        witness_.bytes+=sizeof(plans);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool Sc6ReplayParticleCopy::FreshTraceRenderBinding(const LightingPrimitive& row) const noexcept
{
    if(!fresh_trace_render_proof_.context || !fresh_trace_render_proof_.binding || row.proxy_type!=base_+0x39af350 || row.slot_count!=1)return false;
    __try {
        for(std::size_t i=0;i<fresh_trace_render_count_;++i) {
            const auto& owner=fresh_trace_render_owners_[i];const auto& id=owner.binding.identity;
            if(row.component!=id.target || row.weak!=id.target_weak || row.id!=owner.target_id || row.proxy_inputs[0]!=owner.mesh)continue;
            Sc6ReplayVfxState::ReconstructionBinding current;
            if(!fresh_trace_render_proof_.binding(fresh_trace_render_proof_.context,i,current,false).ok()
                || current.identity.source!=id.source || current.identity.target!=id.target
                || current.identity.source_weak!=id.source_weak || current.identity.target_weak!=id.target_weak
                || current.lease!=owner.binding.lease || Field<unsigned>(row.component,0x420)!=row.id
                || Field<std::uintptr_t>(row.component,0x910)!=owner.mesh || (Field<unsigned>(row.component,0x188)&0xc00000e0u))return false;
            return true;
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool Sc6ReplayParticleCopy::PendingTraceTargetBinding(const LightingPrimitive& row,bool dormant) const noexcept
{
    // Pending metadata is usable only before render ownership handoff. Native
    // reconstruction and its GPU completion must run before simulation admission.
    if(lighting_executing_ || lighting_transferred_ || !witness_.undo_ready
        || !trace_render_owner_ || row.proxy_type!=base_+0x39af350 || row.slot_count!=1)return false;
    __try {
        // Full native trace factories may already own a proxy. Its historical
        // uniform destination stays deferred until normal render execution;
        // native insertion/updates supply the current observation.
        if(FreshTraceRenderBinding(row))return true;
        return trace_render_owner_->RetainedRenderBinding(row.component,row.weak,row.id,row.proxy_inputs[0],false)
            && !(Field<unsigned>(row.component,0x188)&0xc00000e0u)
            && !Field<std::uintptr_t>(row.component,0x790) && !Field<std::uintptr_t>(row.component,0xa18);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool Sc6ReplayParticleCopy::trace_render_pending() const noexcept
{
    return std::any_of(lighting_a_.primitives.begin(),lighting_a_.primitives.end(),[](const auto& row) {
        return row.binding==LightingPrimitive::Binding::PendingTraceTarget;
    });
}

bool Sc6ReplayParticleCopy::PrepareCreationRenderOwners(std::span<const ReplayCreationRenderOwner> target,
    std::span<const ReplayCreationRenderOwner> original,std::size_t budget) noexcept
{
    if(lighting_dirty_ || witness_.target_prepared || target.size()!=original.size()
        || target.size()>creation_render_owners_.size()) return false;
    for(std::size_t i=0;i<target.size();++i) {
        if(!target[i].SameBinding(original[i])) return false;
        for(std::size_t j=0;j<i;++j) if(target[j].component==target[i].component) return false;
    }
    if(creation_render_owner_count_) return creation_render_owner_count_==target.size()
        && std::equal(target.begin(),target.end(),creation_render_owners_.begin());
    if(witness_.bytes>budget || sizeof(creation_render_owners_)>budget-witness_.bytes) return false;
    std::copy(target.begin(),target.end(),creation_render_owners_.begin());
    creation_render_owner_count_=target.size();witness_.bytes+=sizeof(creation_render_owners_);
    return true;
}

bool Sc6ReplayParticleCopy::DormantCreationBinding(const LightingPrimitive& row,bool dormant) const noexcept
{
    __try {
        if(row.proxy_type!=base_+0x39af350 || row.slot_count!=1 || !row.proxy_inputs[0]) return false;
        const auto end=creation_render_owners_.begin()+creation_render_owner_count_;
        const auto owner=std::find_if(creation_render_owners_.begin(),end,
            [&](const auto& x){return x.component==row.component && x.weak==row.weak;});
        if(owner==end || reinterpret_cast<std::uintptr_t>(reinterpret_cast<void*(*)(const void*)>(base_+0xf823f0)(owner->weak.data()))!=row.component
            || reinterpret_cast<std::uintptr_t>(reinterpret_cast<void*(*)(const void*)>(base_+0xf823f0)(owner->owner_weak.data()))!=owner->owner) return false;
        const auto p=row.component;
        if(Field<std::uintptr_t>(p)!=base_+0x38829c0 || Field<std::uintptr_t>(p,0x190)!=owner->owner
            || Field<std::uintptr_t>(p,0x1d0)!=owner->parent
            || owner->slot<0 || owner->slot>=owner->count
            || !owner->ValidMembership(owner->weapon()?owner->owner+0x390:Field<std::uintptr_t>(owner->owner,0x3b0),
                owner->weapon()?1:Field<int>(owner->owner,0x3b8),owner->weapon()?1:Field<int>(owner->owner,0x3bc),
                Field<std::uintptr_t>(owner->storage,owner->slot*8))
            || row.id!=owner->primitive_id || Field<unsigned>(p,0x420)!=row.id || Field<std::uintptr_t>(p,0x910)!=row.proxy_inputs[0]
            || row.proxy_inputs[0]!=owner->mesh || Field<std::uintptr_t>(p,0xab0)!=owner->animation
            || (dormant && (Field<std::uintptr_t>(p,0x790) || Field<std::uintptr_t>(p,0xa18)))
            || (Field<unsigned>(p,0x240)&~0x10u)!=(owner->visibility&~0x10u)
            || (Field<unsigned>(p,0x188)&~0xc0000060u)!=owner->flags) return false;
        // Native141DAE3D0/141DAE6E0 omit a hidden component's mesh object.
        // A visible component is admitted only with owned recreation pending;
        // the enclosing world participant validates its actual queue membership.
        return !dormant || !(Field<unsigned>(p,0x240)&0x10)
            || ((Field<unsigned>(p,0x188)&0x20) && (Field<unsigned>(p,0x188)>>30));
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool Sc6ReplayParticleCopy::PrepareStageRenderOwners(std::span<const ReplayStageVisibility> target,
    std::span<const ReplayStageVisibility> original,std::size_t budget) noexcept
{
    if(lighting_dirty_ || witness_.target_prepared || target.size()!=original.size() || target.size()>128)return false;
    for(std::size_t i=0;i<target.size();++i)if(!target[i].SameBinding(original[i]))return false;
    if(!stage_render_owners_.empty()) {
        if(stage_render_owners_.size()!=original.size())return false;
        for(std::size_t i=0;i<original.size();++i)if(!stage_render_owners_[i].Matches(target[i],original[i]))return false;
        return true;
    }
    if(witness_.bytes>budget || original.size()>(budget-witness_.bytes)/sizeof(ReplayStageRenderOwner))return false;
    try {stage_render_owners_.resize(original.size());}catch(...) {return false;}
    for(std::size_t i=0;i<original.size();++i) {
        stage_render_owners_[i].original=original[i];
        for(unsigned c=0;c<6;++c)stage_render_owners_[i].target_visibility[c]=target[i].components[c].visibility;
    }
    witness_.bytes+=stage_render_owners_.capacity()*sizeof(ReplayStageRenderOwner);
    return witness_.bytes<=budget;
}

bool Sc6ReplayParticleCopy::StageRenderBinding(const LightingPrimitive& primitive,bool dormant) const noexcept
{
    if(!lighting_executing_)return false;
    __try {
        for(const auto& plan:stage_render_owners_)for(const auto& c:plan.original.components)if(c.object==primitive.component) {
            const auto& owner=plan.original;
            const auto resolve=reinterpret_cast<void*(*)(const void*)>(base_+0xf823f0);
            if(c.weak!=primitive.weak || reinterpret_cast<std::uintptr_t>(resolve(c.weak.data()))!=c.object
                || reinterpret_cast<std::uintptr_t>(resolve(owner.weak.data()))!=owner.actor
                || Field<std::uintptr_t>(owner.actor)!=owner.type || Field<std::uintptr_t>(owner.actor,c.member_offset)!=c.object
                || Field<std::uintptr_t>(c.object)!=c.type || Field<std::uintptr_t>(c.object,0x190)!=owner.actor
                || Field<std::uintptr_t>(c.object,0x1d0)!=c.parent || Field<std::uintptr_t>(c.object,c.asset_offset)!=c.asset
                || Field<unsigned>(c.object,0x420)!=primitive.id || primitive.id!=c.primitive_id
                || (Field<unsigned>(c.object,0x240)&~0x10u)!=(c.visibility&~0x10u)
                || (Field<unsigned>(c.object,0x188)&~0xc0000060u)!=(c.flags&~0xc0000060u))return false;
            if(c.asset_offset==0x920) {
                if(primitive.proxy_type!=base_+0x36bd1a8 || primitive.lci_binding.mesh!=c.asset
                    || !c.asset || Field<std::uintptr_t>(c.asset,0x38)!=primitive.lci_binding.render_data)return false;
            } else if(owner.kind!=ReplayStageVisibility::Kind::Emitter || c.type!=base_+0x335db28 || primitive.slot_count!=1)return false;
            // A destination can be absent only after native hidden-component
            // retirement. Visible/pending/unregistered owners still reject.
            return !dormant || (!Field<std::uintptr_t>(c.object,0x790)
                && !(Field<unsigned>(c.object,0x240)&0x10) && !(Field<unsigned>(c.object,0x188)&0xc0000060u));
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool Sc6ReplayParticleCopy::DormantRenderBinding(const LightingPrimitive& row) const noexcept
{
    if(row.binding==LightingPrimitive::Binding::PendingTraceTarget)return PendingTraceTargetBinding(row);
    if(row.binding==LightingPrimitive::Binding::PendingParticleTarget)return PendingParticleTargetBinding(row);
    if(row.binding==LightingPrimitive::Binding::PendingStageTarget)return PendingStageTargetBinding(row);
    return DormantCreationBinding(row) || StageRenderBinding(row) || TraceRenderBinding(row);
}

bool Sc6ReplayParticleCopy::ReadParticleRenderOwner(ParticleRenderOwner& owner) const noexcept
{
    const auto& binding=owner.binding;
    const auto& id=binding.identity;
    // Full lease membership is admitted on the game thread by Prepare below.
    // The held render boundary may only inspect registration and the indexed
    // weak generation; ValidateObject requires the native game thread.
    if(!binding.lease || !binding.lease->registered())return false;
    __try {
        if(!id.source || !id.target || id.source==id.target
            || reinterpret_cast<std::uintptr_t>(reinterpret_cast<void*(*)(const void*)>(base_+0xf823f0)(id.target_weak.data()))!=id.target
            || Field<std::uintptr_t>(id.target)!=base_+0x335db28
            || Field<std::uintptr_t>(id.target,0x1c8)!=world_)return false;
        owner.target_id=Field<unsigned>(id.target,0x420);
        owner.particle_template=Field<std::uintptr_t>(id.target,0x808);
        return owner.target_id && owner.particle_template;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool Sc6ReplayParticleCopy::PrepareParticleRenderOwners(const Sc6ReplayVfxState& target,
    std::span<const Sc6ReplayVfxState::ReconstructionBinding> bindings,std::size_t budget) noexcept
{
    if(witness_.phase!=Phase::UndoReady || !witness_.undo_ready || !completion_.retired()
        || witness_.target_prepared || lighting_dirty_ || lighting_transferred_ || lighting_executing_
        || !lighting_a_.captured || !lighting_b_.captured || particle_render_owner_count_
        || !target.ValidateReconstructionBindings(bindings).ok())return false;
    std::array<ParticleRenderOwner,32> plans{};
    std::array<ReplayLightingBinding::PrimitiveIdReplacement,32> keys{};
    std::size_t count{};
    for(const auto& binding:bindings) {
        const auto& id=binding.identity;
        if(id.source==id.target)continue;
        if(count==plans.size())return false;
        auto& plan=plans[count];plan.binding=binding;
        if(!ReadParticleRenderOwner(plan))return false;
        plan.emitter_counts=target.ParticleEmitterCounts(id.source);
        if(plan.emitter_counts[0]+plan.emitter_counts[1]>128 || (plan.emitter_counts[0]==0 && plan.emitter_counts[1]==0))return false;
        const auto row=std::find_if(lighting_a_.primitives.begin(),lighting_a_.primitives.end(),
            [&](const auto& row){return row.component==id.source && row.weak==id.source_weak;});
        if(row==lighting_a_.primitives.end() || !row->id || row->id==plan.target_id || row->slot_count!=1
            || (row->proxy_type!=base_+0x3956e80 && row->proxy_type!=base_+0x3956f98)
            || std::count_if(lighting_a_.primitives.begin(),lighting_a_.primitives.end(),
                [&](const auto& p){return p.component==id.source;})!=1
            || std::count_if(lighting_a_.uniforms.begin(),lighting_a_.uniforms.end(),
                [&](const auto& u){return u.component==id.source;})!=1)return false;
        for(const auto& u:lighting_a_.uniforms)if(u.component==id.source && u.ordinal!=0)return false;
        for(const auto* image:{&lighting_a_,&lighting_b_})for(const auto& p:image->primitives)
            if(p.component==id.target || p.id==plan.target_id
                || (image==&lighting_b_ && (p.component==id.source || p.id==row->id)))return false;
        for(std::size_t i=0;i<count;++i)if(plans[i].binding.identity.source==id.source
            || plans[i].binding.identity.target==id.target || plans[i].target_id==plan.target_id)return false;
        plan.source_id=row->id;plan.proxy_type=row->proxy_type;
        keys[count++]={row->id,plan.target_id};
    }
    if(!count)return true;
    if(witness_.bytes>budget || sizeof(plans)>budget-witness_.bytes)return false;
    auto& map=lighting_a_.maps[1];
    unsigned rows{};std::memcpy(&rows,map.header.data()+8,4);
    if(!ReplayLightingBinding::RebindPrimitiveAllocationKeys(map.entries,rows,map.flags,map.hashes,
        std::span(keys).first(count)))return false;
    // All fallible validation precedes private metadata writes. The checkpoint
    // and complete B are untouched; only this operation's A image is projected.
    std::uintptr_t hash_heap{};std::memcpy(&hash_heap,map.header.data()+0x40,8);
    if(!hash_heap)std::memcpy(map.header.data()+0x38,map.hashes.data(),4);
    for(std::size_t i=0;i<count;++i) {
        const auto& plan=plans[i];const auto& id=plan.binding.identity;
        for(auto& row:lighting_a_.primitives)if(row.component==id.source) {
            row.component=id.target;row.weak=id.target_weak;row.id=plan.target_id;
            row.address=row.proxy=0;row.slots={};row.binding=LightingPrimitive::Binding::PendingParticleTarget;
        }
        for(auto& u:lighting_a_.uniforms)if(u.component==id.source){u.component=id.target;u.slot=0;}
        for(auto& owner:lighting_a_.owners)if(owner==reinterpret_cast<void*>(id.source))owner=reinterpret_cast<void*>(id.target);
    }
    particle_render_owners_=plans;particle_render_owner_count_=count;witness_.bytes+=sizeof(plans);
    return true;
}

bool Sc6ReplayParticleCopy::PendingParticleTargetBinding(const LightingPrimitive& row) const noexcept
{
    if(lighting_executing_ || lighting_transferred_ || !witness_.undo_ready || row.slot_count!=1)return false;
    for(std::size_t i=0;i<particle_render_owner_count_;++i) {
        const auto& plan=particle_render_owners_[i];const auto& id=plan.binding.identity;
        if(row.component!=id.target || row.weak!=id.target_weak || row.id!=plan.target_id || row.proxy_type!=plan.proxy_type)continue;
        auto current=plan;
        return ReadParticleRenderOwner(current) && current.target_id==plan.target_id
            && current.particle_template==plan.particle_template;
    }
    return false;
}

bool Sc6ReplayParticleCopy::BindParticleRenderOwners() noexcept
{
    if(!particle_render_owner_count_)return true;
    if(particle_render_bound_)return LightingBindings(lighting_a_);
    if(witness_.phase!=Phase::Installed || !completion_.retired() || !lighting_dirty_
        || !witness_.native_write_uncommitted || lighting_executing_ || lighting_transferred_)return false;
    std::array<LightingPrimitive,32> destinations{};
    __try {
        const auto count=Field<int>(scene_,0xad0);const auto data=Field<std::uintptr_t>(scene_,0xac8);
        if(count<0 || count>1024 || (count && !data))return false;
        for(std::size_t i=0;i<particle_render_owner_count_;++i) {
            const auto& plan=particle_render_owners_[i];
            const auto saved=std::find_if(lighting_a_.primitives.begin(),lighting_a_.primitives.end(),
                [&](const auto& p){return p.component==plan.binding.identity.target;});
            if(saved==lighting_a_.primitives.end() || !PendingParticleTargetBinding(*saved))return false;
            auto& row=destinations[i];row=*saved;
            row.proxy=Field<std::uintptr_t>(row.component,0x790);
            if(!row.proxy || Field<std::uintptr_t>(row.proxy)!=row.proxy_type)return false;
            row.address=Field<std::uintptr_t>(row.proxy,0x118);
            if(!row.address || Field<std::uintptr_t>(row.address,0xe8)!=row.component
                || Field<std::uintptr_t>(row.address,0xd8)!=scene_ || Field<unsigned>(row.address,0x10)!=row.id)return false;
            const auto index=Field<int>(row.address,0xe4);
            if(index<0 || index>=count || Field<std::uintptr_t>(data,std::size_t(index)*8)!=row.address
                || !LightingPrimitiveSlots(row) || row.slot_count!=1
                || !VisibilityWritable(row.address+0x50,16) || !VisibilityWritable(row.address+0xf2,1))return false;
            for(const auto& b:lighting_b_.primitives)if(b.address==row.address || b.proxy==row.proxy)return false;
            for(std::size_t j=0;j<i;++j)if(destinations[j].address==row.address || destinations[j].proxy==row.proxy)return false;
            if(row.allocation && std::none_of(lighting_a_.allocations.begin(),lighting_a_.allocations.end(),
                [&](const auto& a){return a.address==row.allocation && a.installed;}))return false;
            row.binding=LightingPrimitive::Binding::ScenePrimitive;
        }
        const auto assign=reinterpret_cast<void**(*)(void**,void*)>(base_+0x15b83f0);
        for(std::size_t i=0;i<particle_render_owner_count_;++i) {
            const auto& row=destinations[i];std::uintptr_t allocation{};
            for(const auto& a:lighting_a_.allocations)if(a.address==row.allocation)allocation=reinterpret_cast<std::uintptr_t>(a.installed);
            for(auto& u:lighting_a_.uniforms)if(u.component==row.component) {
                assign(reinterpret_cast<void**>(row.slots[0]),u.value);u.slot=row.slots[0];
            }
            Field<std::uintptr_t>(row.address,0x50)=allocation;Field<std::uint8_t>(row.address,0xf2)=row.dirty;
            for(auto& saved:lighting_a_.primitives)if(saved.component==row.component)saved=row;
        }
        particle_render_bound_=true;
        return LightingBindings(lighting_a_);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool Sc6ReplayParticleCopy::CompleteParticleRenderOwners() noexcept
{
    if((!particle_render_owner_count_ && !trace_render_pending()) || particle_render_bound_ || !execution_started_ || execution_settled_
        || !lighting_executing_ || !registry_executing_ || witness_.phase!=Phase::Installed
        || !completion_.retired() || !witness_.native_write_uncommitted)return false;
    __try {
        const auto count=Field<int>(scene_,0xad0),bits=Field<int>(system_,0x68);
        const auto scene_data=Field<std::uintptr_t>(scene_,0xac8),registry=Field<std::uintptr_t>(system_,0x40);
        const auto flags=Field<std::uintptr_t>(system_,0x60);
        if(count<0 || count>1024 || (count && !scene_data) || bits<0 || bits>65536
            || Field<int>(system_,0x48)!=bits || (bits && !registry))return false;
        for(std::size_t i=0;i<particle_render_owner_count_;++i) {
            auto plan=particle_render_owners_[i];const auto& id=plan.binding.identity;
            if(!ReadParticleRenderOwner(plan) || plan.target_id!=particle_render_owners_[i].target_id
                || plan.particle_template!=particle_render_owners_[i].particle_template)return false;
            std::array<std::uintptr_t,128> renders{};std::size_t gpu{};
            for(const auto& coordinate:coordinates_) {
                if(!coordinate.fresh || coordinate.component!=id.target || coordinate.component_weak!=id.target_weak)continue;
                if(gpu==renders.size() || !CoordinatesBound(coordinate,true))return false;
                for(std::size_t n=0;n<gpu;++n)if(renders[n]==coordinate.render)return false;
                renders[gpu++]=coordinate.render;
                const auto index=Field<int>(coordinate.render,0x240);
                if(index<0 || index>=bits || Field<std::uintptr_t>(registry,std::size_t(index)*8)!=coordinate.render
                    || !(Field<unsigned>(flags?flags:system_+0x50,std::size_t(index/32)*4)&(1u<<(index%32))))return false;
            }
            if(gpu!=plan.emitter_counts[1])return false;
            if(!gpu) {
                if(!plan.emitter_counts[0])return false;
                const auto slots=Field<std::uintptr_t>(id.target,0xa50);
                const auto size=Field<int>(id.target,0xa58),capacity=Field<int>(id.target,0xa5c);
                if(!slots || size<=0 || size>128 || capacity<size || capacity>128)return false;
                std::size_t cpu{};
                for(int slot=0;slot<size;++slot)if(const auto root=Field<std::uintptr_t>(slots,std::size_t(slot)*8)) {
                    const auto type=Field<std::uintptr_t>(root);
                    if((type!=base_+0x3949b60 && type!=base_+0x3949d88) || Field<std::uintptr_t>(root,0x18)!=id.target)return false;
                    ++cpu;
                }
                if(cpu!=plan.emitter_counts[0])return false;
            }
            LightingPrimitive row{};row.component=id.target;row.weak=id.target_weak;row.id=plan.target_id;row.proxy_type=plan.proxy_type;
            row.proxy=Field<std::uintptr_t>(id.target,0x790);
            if(!row.proxy || Field<std::uintptr_t>(row.proxy)!=plan.proxy_type)return false;
            row.address=Field<std::uintptr_t>(row.proxy,0x118);
            if(!row.address || Field<std::uintptr_t>(row.address,0xe8)!=id.target
                || Field<std::uintptr_t>(row.address,0xd8)!=scene_ || Field<unsigned>(row.address,0x10)!=plan.target_id)return false;
            const auto scene_index=Field<int>(row.address,0xe4);
            if(scene_index<0 || scene_index>=count || Field<std::uintptr_t>(scene_data,std::size_t(scene_index)*8)!=row.address
                || !LightingPrimitiveSlots(row) || row.slot_count!=1)return false;
            for(const auto& b:lighting_b_.primitives)if(b.address==row.address || b.proxy==row.proxy)return false;
        }
        for(const auto& saved:lighting_a_.primitives)if(saved.binding==LightingPrimitive::Binding::PendingTraceTarget) {
            if(!TraceRenderBinding(saved,false))return false;
            auto row=saved;row.proxy=Field<std::uintptr_t>(row.component,0x790);
            if(!row.proxy || Field<std::uintptr_t>(row.proxy)!=row.proxy_type
                || !Field<std::uintptr_t>(row.component,0xa18))return false;
            row.address=Field<std::uintptr_t>(row.proxy,0x118);
            if(!row.address || Field<std::uintptr_t>(row.address,0xe8)!=row.component
                || Field<std::uintptr_t>(row.address,0xd8)!=scene_ || Field<unsigned>(row.address,0x10)!=row.id)return false;
            const auto index=Field<int>(row.address,0xe4);
            if(index<0 || index>=count || Field<std::uintptr_t>(scene_data,std::size_t(index)*8)!=row.address
                || !LightingPrimitiveSlots(row) || row.slot_count!=1)return false;
        }
        // Native scene insertion owns its lighting invalidation and uniforms.
        // Do not overwrite those observations with A, or touch private B.
        if(!DrainExecutionWork())return false;
        particle_render_bound_=true;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool Sc6ReplayParticleCopy::PendingStageTargetBinding(const LightingPrimitive& row) const noexcept
{
    if(lighting_executing_ || lighting_transferred_ || !witness_.undo_ready)return false;
    __try {
        const auto read=[](std::uintptr_t p,auto& value){std::memcpy(&value,reinterpret_cast<void*>(p),sizeof(value));return true;};
        const auto resolve=[&](const auto& weak){return reinterpret_cast<std::uintptr_t>(
            reinterpret_cast<void*(*)(const void*)>(base_+0xf823f0)(weak.data()));};
        for(const auto& plan:stage_render_owners_)
            if(plan.TargetDestination(base_,row.component,row.weak,row.id,row.proxy_type,row.slot_count,
                row.lci_binding.mesh,row.lci_binding.render_data,read,resolve))return true;
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool Sc6ReplayParticleCopy::LightingPrimitiveSlots(LightingPrimitive& row) const noexcept
{
    struct InlinePointers { std::array<std::uintptr_t,8> local{}; std::uintptr_t heap{}; int count{},capacity{}; } list;
    bool result=false;
    __try {
        row.slot_count=0;row.slots={};row.lci_binding={};
        row.slots[row.slot_count++]=row.address+0x58;
        const auto table=Field<std::uintptr_t>(row.proxy);
        reinterpret_cast<void(*)(void*,void*)>(Field<std::uintptr_t>(table,0x100))(reinterpret_cast<void*>(row.proxy),&list);
        if(list.count<0 || list.count>16 || list.capacity<list.count || (!list.heap && list.count>8)) __leave;
        for(int i=0;i<list.count;++i) {
            const auto lci=list.heap?Field<std::uintptr_t>(list.heap,std::size_t(i)*8):list.local[i];
            if(!lci) continue;
            const auto slot=lci+0x18;
            if(!VisibilityWritable(slot,8)) __leave;
            bool duplicate=false;
            for(unsigned j=0;j<row.slot_count;++j) duplicate|=row.slots[j]==slot;
            if(!duplicate) row.slots[row.slot_count++]=slot;
        }
        if(table==base_+0x36bd1a8) {
            // 142128890 enumerates proxy+1C0 in LOD order. Constructor
            // 14211ECF0 creates one 14211DEC0 LCI per mesh render-data LOD.
            // Its +8/+10 bindings are light/shadow maps; +18 is the mutable
            // uniform. Destruction frees the LCI, so only live rows are read.
            constexpr unsigned char signature[]{0x40,0x53,0x57,0x41,0x56,0x48,0x83,0xec,0x20,0x33,0xff,0x48,0x8b,0xda,0x4c,0x8b,0xf1};
            if(Field<std::uintptr_t>(table,0x100)!=base_+0x2128890
                || std::memcmp(reinterpret_cast<void*>(base_+0x2128890),signature,sizeof(signature))) __leave;
            auto& binding=row.lci_binding;
            binding.mesh=Field<std::uintptr_t>(row.proxy,0x1a8);
            binding.render_data=Field<std::uintptr_t>(row.proxy,0x1b8);
            if(!binding.mesh || !binding.render_data || list.count<1 || list.count>16
                || row.slot_count!=unsigned(list.count)+1
                || Field<std::uintptr_t>(row.component,0x920)!=binding.mesh
                || Field<std::uintptr_t>(binding.mesh,0x38)!=binding.render_data
                || Field<int>(binding.render_data,8)!=list.count
                || Field<int>(row.proxy,0x1c8)!=list.count) __leave;
            const auto lods=Field<std::uintptr_t>(binding.render_data);
            const auto lcis=Field<std::uintptr_t>(row.proxy,0x1c0);
            if(!lods || !lcis) __leave;
            binding.count=unsigned(list.count);
            for(unsigned i=0;i<binding.count;++i) {
                const auto lci=Field<std::uintptr_t>(lcis,i*8);
                if(!lci || row.slots[i+1]!=lci+0x18 || Field<std::uintptr_t>(lci)!=base_+0x39b9280) __leave;
                binding.lods[i]={Field<std::uintptr_t>(lods,i*8),Field<std::uintptr_t>(lci,8),Field<std::uintptr_t>(lci,0x10)};
            }
            if(!binding.Matches(binding,row.slot_count)) __leave;
        }
        result=true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {result=false;}
    if(list.heap) reinterpret_cast<void(*)(void*)>(base_+0xd46a00)(reinterpret_cast<void*>(list.heap));
    return result;
}

bool Sc6ReplayParticleCopy::CaptureLighting(LightingImage& image,std::size_t budget) noexcept
{
    try {return CaptureLightingProtected(image,budget);} catch(...) {LIGHTING_REJECT();}
}
bool Sc6ReplayParticleCopy::CaptureLightingProtected(LightingImage& image,std::size_t budget)
{
    __try {return CaptureLightingUnchecked(image,budget);}
    __except(GetExceptionCode()==0xe06d7363u?EXCEPTION_CONTINUE_SEARCH:EXCEPTION_EXECUTE_HANDLER) {LIGHTING_REJECT();}
}
bool Sc6ReplayParticleCopy::CaptureLightingUnchecked(LightingImage& image,std::size_t budget)
{
    if(image.captured || !image.primitives.empty()) LIGHTING_REJECT();
    const auto cache=scene_+0xda8;
    // Point paths are admitted from actual allocations, not the global
    // volume-texture initialization flag. Texture-sampled allocations fail.
    if(Field<unsigned char>(cache,0x30)>1) LIGHTING_REJECT();
    image.next_point=Field<unsigned>(cache,0x138);image.update_all=Field<unsigned char>(cache,0x30);
    const int primitive_count=Field<int>(scene_,0xad0);
    if(primitive_count<0 || primitive_count>1024) LIGHTING_REJECT();
    // Bounded CPU vectors and native query scratch are charged before use.
    const std::size_t reserve=sizeof(LightingImage)+128*sizeof(LightingAllocation)
        +1024*(sizeof(LightingPrimitive)+sizeof(void*))+4096*sizeof(LightingUniform)+65536;
    if(witness_.bytes>budget || reserve>budget-witness_.bytes) LIGHTING_REJECT();
    witness_.bytes+=reserve;
    image.allocations.reserve(128);image.primitives.reserve(1024);image.owners.reserve(1024);image.uniforms.reserve(4096);
    const auto charge=[&](std::size_t bytes) {
        if(witness_.bytes>budget || bytes>budget-witness_.bytes) LIGHTING_REJECT();
        witness_.bytes+=bytes;return true;
    };
    for(unsigned kind=0;kind<2;++kind) {
        const auto address=cache+(kind?0x140:0x50);
        auto& map=image.maps[kind];
        const int count=Field<int>(address,8),capacity=Field<int>(address,0xc),bits=Field<int>(address,0x28);
        const int bit_capacity=Field<int>(address,0x2c),free=Field<int>(address,0x34),hash_count=Field<int>(address,0x48);
        const auto data=Field<std::uintptr_t>(address),flag_heap=Field<std::uintptr_t>(address,0x20),hash_heap=Field<std::uintptr_t>(address,0x40);
        if(count<0 || count>128 || capacity<count || capacity>256 || bits!=count || bit_capacity<bits
            || bit_capacity>256 || bit_capacity%32 || free<0 || free>count || hash_count<1 || hash_count>512
            || (hash_count&(hash_count-1)) || (!flag_heap && bit_capacity>128) || (!hash_heap && hash_count!=1)
            || (capacity && !data)) LIGHTING_REJECT();
        const std::size_t stride=kind?0x18:0x40;
        if(!charge(std::size_t(capacity)*stride+std::size_t(bit_capacity)/8+std::size_t(hash_count)*4)) LIGHTING_REJECT();
        map.entries.resize(std::size_t(capacity)*stride);map.flags.resize(std::size_t(bit_capacity)/8);map.hashes.resize(std::size_t(hash_count)*4);
        std::memcpy(map.header.data(),reinterpret_cast<void*>(address),0x50);
        if(!ReadBytes(map.entries.data(),reinterpret_cast<void*>(data),map.entries.size())
            || !ReadBytes(map.flags.data(),reinterpret_cast<void*>(flag_heap?flag_heap:address+0x10),map.flags.size())
            || !ReadBytes(map.hashes.data(),reinterpret_cast<void*>(hash_heap?hash_heap:address+0x38),map.hashes.size())) LIGHTING_REJECT();
        unsigned occupied{};
        for(int i=0;i<count;++i) {
            if(!(Field<unsigned>(reinterpret_cast<std::uintptr_t>(map.flags.data()),std::size_t(i/32)*4)&(1u<<(i%32)))) continue;
            ++occupied;
            if(!kind) continue;
            const auto entry=data+std::size_t(i)*stride;
            const auto allocation=Field<std::uintptr_t>(entry,8);
            if(!allocation || Field<unsigned char>(allocation,0x181)!=1 || Field<int>(allocation,0x3c)!=1
                || Field<unsigned char>(allocation,0x180)>1 || Field<unsigned char>(allocation,0x182)>1
                || Field<unsigned char>(allocation,0x183)>1) LIGHTING_REJECT();
            if(std::find_if(image.allocations.begin(),image.allocations.end(),[&](const auto& row){return row.address==allocation;})!=image.allocations.end()) LIGHTING_REJECT();
            auto& row=image.allocations.emplace_back();row.address=allocation;
            std::memcpy(row.values.data(),reinterpret_cast<void*>(allocation),row.values.size());
        }
        if(occupied!=unsigned(count-free)) LIGHTING_REJECT();
    }
    const auto primitive_data=Field<std::uintptr_t>(scene_,0xac8);
    if(primitive_count && !primitive_data) LIGHTING_REJECT();
    const auto assign=reinterpret_cast<void**(*)(void**,void*)>(base_+0x15b83f0);
    std::size_t quarantined_seen{};
    for(int i=0;i<primitive_count;++i) {
        auto& row=image.primitives.emplace_back();row.address=Field<std::uintptr_t>(primitive_data,std::size_t(i)*8);
        if(!row.address || Field<std::uintptr_t>(row.address,0xd8)!=scene_ || Field<int>(row.address,0xe4)!=i) LIGHTING_REJECT();
        row.proxy=Field<std::uintptr_t>(row.address,8);row.component=Field<std::uintptr_t>(row.address,0xe8);
        row.id=Field<unsigned>(row.address,0x10);row.allocation=Field<std::uintptr_t>(row.address,0x50);row.dirty=Field<unsigned char>(row.address,0xf2);
        // 1414CF550 obtains the occlusion key from this parallel primitive-ID
        // array. Verify correspondence rather than inferring it from draw order.
        if(!Field<std::uintptr_t>(scene_,0xb38)
            || Field<unsigned>(Field<std::uintptr_t>(scene_,0xb38),std::size_t(i)*4)!=row.id) LIGHTING_REJECT();
        // Constructor141490740 binds component+790, ID+420 and owner+E8.
        if(!row.proxy || !row.component || Field<std::uintptr_t>(row.proxy,0x118)!=row.address
            || Field<std::uintptr_t>(row.component,0x790)!=row.proxy || Field<unsigned>(row.component,0x420)!=row.id || row.dirty>1) LIGHTING_REJECT();
        row.proxy_type=Field<std::uintptr_t>(row.proxy);
        if(row.proxy_type==base_+0x39af350) {
            // 141DC0EA0 proxy admission: mesh, mesh object, predicted LOD,
            // hide-skin byte; registration flags are an independent witness.
            row.proxy_inputs={Field<std::uintptr_t>(row.component,0x910),Field<std::uintptr_t>(row.component,0xa18),
                Field<unsigned>(row.component,0x9b8),Field<unsigned char>(row.component,0x9e0),Field<unsigned>(row.component,0x188),
                Field<unsigned>(row.component,0x240),Field<unsigned>(row.component,0x3fc)};
        }
        reinterpret_cast<void(*)(void*,const void*)>(base_+0xf7bad0)(row.weak.data(),reinterpret_cast<void*>(row.component));
        if(reinterpret_cast<void*(*)(const void*)>(base_+0xf823f0)(row.weak.data())!=reinterpret_cast<void*>(row.component)) LIGHTING_REJECT();
        if(row.allocation && std::find_if(image.allocations.begin(),image.allocations.end(),[&](const auto& x){return x.address==row.allocation;})==image.allocations.end()) LIGHTING_REJECT();
        if(!LightingPrimitiveSlots(row)) LIGHTING_REJECT();
        if(&image==&lighting_a_ && std::find(capture_quarantined_.begin(),
            capture_quarantined_.begin()+capture_quarantined_count_,row.id)!=capture_quarantined_.begin()+capture_quarantined_count_) {
            // PublishLighting clears B-only destinations when A is installed.
            // The native proxy stays allocated solely for complete-B undo and
            // the existing view mask excludes it from corrected rendering.
            // It is not part of a reusable corrected checkpoint. Prove there
            // is no allocation/publication/work to discard before omitting it.
            if(row.allocation || row.dirty) LIGHTING_REJECT();
            for(unsigned j=0;j<row.slot_count;++j)if(Field<void*>(row.slots[j])) LIGHTING_REJECT();
            const auto& map=image.maps[1];
            const auto rows=Field<int>(reinterpret_cast<std::uintptr_t>(map.header.data()),8);
            for(int j=0;j<rows;++j)
                if((Field<unsigned>(reinterpret_cast<std::uintptr_t>(map.flags.data()),std::size_t(j/32)*4)&(1u<<(j%32)))
                    && Field<unsigned>(reinterpret_cast<std::uintptr_t>(map.entries.data()),std::size_t(j)*0x18)==row.id) LIGHTING_REJECT();
            ++quarantined_seen;image.primitives.pop_back();continue;
        }
        image.owners.push_back(reinterpret_cast<void*>(row.component));
        for(unsigned j=0;j<row.slot_count;++j) {
            const auto slot=row.slots[j];
            if(std::find_if(image.uniforms.begin(),image.uniforms.end(),[&](const auto& x){return x.slot==slot;})!=image.uniforms.end()) continue;
            std::size_t uniform_bytes{};
            auto* value=Field<void*>(slot);
            if(image.uniforms.size()==4096 || !RetainedUniformBytes(value,uniform_bytes) || !charge(uniform_bytes)) LIGHTING_REJECT();
            auto& uniform=image.uniforms.emplace_back();uniform.slot=slot;uniform.component=row.component;uniform.ordinal=j;
            assign(&uniform.value,value);
        }
    }
    if(&image==&lighting_a_ && quarantined_seen!=capture_quarantined_count_) LIGHTING_REJECT();
    image.captured=true;
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] lighting captured target={} primitives={} allocations={} uniform_slots={} point_only=true bytes={}\n"),
        &image==&lighting_a_,image.primitives.size(),image.allocations.size(),image.uniforms.size(),witness_.bytes);
    return true;
}

bool Sc6ReplayParticleCopy::LightingBindings(const LightingImage& image) const noexcept
{
    __try {
        if(!image.captured || Field<std::uintptr_t>(world_,0x168)!=scene_) LIGHTING_REJECT();
        const auto count=Field<int>(scene_,0xad0);
        const auto data=Field<std::uintptr_t>(scene_,0xac8);
        if(count<0 || count>1024 || (count && !data)) LIGHTING_REJECT();
        for(const auto& saved:image.primitives) {
            if(saved.binding!=LightingPrimitive::Binding::ScenePrimitive) {
                if(!DormantRenderBinding(saved)) LIGHTING_REJECT();
                continue;
            }
            // Resolve the UObject generation first, before following any
            // captured component/proxy or light-cache slot address.
            const auto resolved=reinterpret_cast<void*(*)(const void*)>(base_+0xf823f0)(saved.weak.data());
            if(resolved!=reinterpret_cast<void*>(saved.component)) {
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] lighting binding lifetime target={} component={:x} resolved={:x}\n"),
                    &image==&lighting_a_,saved.component,reinterpret_cast<std::uintptr_t>(resolved));
                LIGHTING_REJECT();
            }
            const auto proxy=Field<std::uintptr_t>(saved.component,0x790);
            const auto id=Field<unsigned>(saved.component,0x420);
            // Do not dereference the historical proxy once the live component
            // no longer owns it. UObject leases do not prove proxy lifetime.
            if(proxy!=saved.proxy || id!=saved.id) {
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] lighting binding component target={} component={:x} id={}/{} proxy={:x}/{:x}\n"),
                    &image==&lighting_a_,saved.component,id,saved.id,proxy,saved.proxy);
                LIGHTING_REJECT();
            }
            if(Field<std::uintptr_t>(proxy,0x118)!=saved.address || Field<std::uintptr_t>(saved.address,0xe8)!=saved.component
                || Field<std::uintptr_t>(saved.address,0xd8)!=scene_) {
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] lighting binding scene target={} component={:x} proxy={:x} info={:x}/{:x}\n"),
                    &image==&lighting_a_,saved.component,proxy,Field<std::uintptr_t>(proxy,0x118),saved.address);
                LIGHTING_REJECT();
            }
            const int index=Field<int>(saved.address,0xe4);
            if(index<0 || index>=count || Field<std::uintptr_t>(data,std::size_t(index)*8)!=saved.address) LIGHTING_REJECT();
            auto observed=saved;
            if(!LightingPrimitiveSlots(observed) || observed.slot_count!=saved.slot_count || observed.slots!=saved.slots
                || observed.lci_binding!=saved.lci_binding
                || !VisibilityWritable(saved.address+0x50,8) || !VisibilityWritable(saved.address+0xf2,1)) LIGHTING_REJECT();
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {LIGHTING_REJECT();}
}

bool Sc6ReplayParticleCopy::LightingMapMatches(const LightingMap& image,std::uintptr_t address,bool installed) const noexcept
{
    __try {
        auto header=image.header;
        const auto h=reinterpret_cast<std::uintptr_t>(header.data());
        if(installed) {
            for(unsigned i=0;i<3;++i) if(image.installed[i]) Field<void*>(h,i==0?0:i==1?0x20:0x40)=image.installed[i];
        }
        if(std::memcmp(header.data(),reinterpret_cast<void*>(address),0x50)) LIGHTING_REJECT();
        const auto data=Field<std::uintptr_t>(address),flags=Field<std::uintptr_t>(address,0x20),hash=Field<std::uintptr_t>(address,0x40);
        if(!image.entries.empty() && std::memcmp(image.entries.data(),reinterpret_cast<void*>(data),image.entries.size())) LIGHTING_REJECT();
        if(!image.flags.empty() && std::memcmp(image.flags.data(),reinterpret_cast<void*>(flags?flags:address+0x10),image.flags.size())) LIGHTING_REJECT();
        return image.hashes.empty() || !std::memcmp(image.hashes.data(),reinterpret_cast<void*>(hash?hash:address+0x38),image.hashes.size());
    } __except(EXCEPTION_EXECUTE_HANDLER) {LIGHTING_REJECT();}
}

bool Sc6ReplayParticleCopy::LightingImageMatches(bool original) const noexcept
{
    __try {
        const auto& image=original?lighting_a_:lighting_b_;
        const auto cache=scene_+0xda8;
        if(!LightingBindings(lighting_b_) || (original && !LightingBindings(lighting_a_))) LIGHTING_REJECT();
        for(unsigned i=0;i<2;++i) if(!LightingMapMatches(image.maps[i],cache+(i?0x140:0x50),original)) LIGHTING_REJECT();
        if(Field<unsigned>(cache,0x138)!=image.next_point || Field<std::uint8_t>(cache,0x30)!=image.update_all) LIGHTING_REJECT();
        for(const auto& row:image.allocations)
            if(std::memcmp(row.values.data(),original?row.installed:reinterpret_cast<void*>(row.address),row.values.size())) LIGHTING_REJECT();
        const auto& destinations=lighting_executing_?lighting_a_:lighting_b_;
        for(const auto& row:destinations.uniforms) {
            if(!row.slot) continue;
            const auto found=std::find_if(image.uniforms.begin(),image.uniforms.end(),[&](const auto& saved){return saved.slot==row.slot;});
            if(Field<void*>(row.slot)!=(found==image.uniforms.end()?nullptr:found->value)) LIGHTING_REJECT();
        }
        for(const auto& row:destinations.primitives) {
            if(row.binding!=LightingPrimitive::Binding::ScenePrimitive) continue;
            const auto found=std::find_if(image.primitives.begin(),image.primitives.end(),[&](const auto& saved){return saved.address==row.address;});
            std::uintptr_t allocation{};std::uint8_t dirty{};
            if(found!=image.primitives.end()) {
                allocation=found->allocation;dirty=found->dirty;
                if(original && allocation) {
                    const auto value=std::find_if(image.allocations.begin(),image.allocations.end(),[&](const auto& saved){return saved.address==allocation;});
                    if(value==image.allocations.end() || !value->installed) LIGHTING_REJECT();
                    allocation=reinterpret_cast<std::uintptr_t>(value->installed);
                }
            }
            if(Field<std::uintptr_t>(row.address,0x50)!=allocation || Field<std::uint8_t>(row.address,0xf2)!=dirty) LIGHTING_REJECT();
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {LIGHTING_REJECT();}
}

bool Sc6ReplayParticleCopy::RebindLighting() noexcept
{
    return !lighting_dirty_ && !lighting_transferred_ && RebindLightingImage(lighting_a_,lighting_b_);
}

void Sc6ReplayParticleCopy::DescribeLightingDomainDelta(const LightingImage& image,const LightingImage& live_image) const noexcept
{
    try {
    unsigned missing{},replaced{},born{};
    for(const auto& saved:image.primitives) {
        const auto live=std::find_if(live_image.primitives.begin(),live_image.primitives.end(),
            [&](const auto& row){return row.component==saved.component && row.id==saved.id && row.weak==saved.weak;});
        if(live!=live_image.primitives.end() && live->proxy==saved.proxy && live->address==saved.address) continue;
        const bool absent=live==live_image.primitives.end();
        if(absent) ++missing;else ++replaced;
        if(missing+replaced>64) continue;
        const auto resolved=reinterpret_cast<void*(*)(const void*)>(base_+0xf823f0)(saved.weak.data());
        if(resolved!=reinterpret_cast<void*>(saved.component)) {
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] lighting domain owner expired id={} type={:x}\n"),saved.id,saved.proxy_type-base_);
            continue;
        }
        const auto name=reinterpret_cast<RC::Unreal::UObject*>(resolved)->GetFullName();
        std::array<std::uintptr_t,7> now{};
        if(saved.proxy_type==base_+0x39af350)
            now={Field<std::uintptr_t>(saved.component,0x910),Field<std::uintptr_t>(saved.component,0xa18),
                Field<unsigned>(saved.component,0x9b8),Field<unsigned char>(saved.component,0x9e0),Field<unsigned>(saved.component,0x188),
                Field<unsigned>(saved.component,0x240),Field<unsigned>(saved.component,0x3fc)};
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] lighting domain delta missing={} id={} type={:x} name={} mesh={:x}/{:x} object={:x}/{:x} lod={}/{} skin={:x}/{:x} flags={:x}/{:x}\n"),
            absent,saved.id,saved.proxy_type-base_,name,saved.proxy_inputs[0],now[0],saved.proxy_inputs[1],now[1],
            saved.proxy_inputs[2],now[2],saved.proxy_inputs[3],now[3],saved.proxy_inputs[4],now[4]);
        if(absent) RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] lighting missing visibility id={} visibility={:x}/{:x} render_flags={:x}/{:x} owner={:x} parent={:x} anim={:x}\n"),
            saved.id,saved.proxy_inputs[5],now[5],saved.proxy_inputs[6],now[6],Field<std::uintptr_t>(saved.component,0x190),
            Field<std::uintptr_t>(saved.component,0x1d0),Field<std::uintptr_t>(saved.component,0xab0));
    }
    for(const auto& live:live_image.primitives) {
        if(std::find_if(image.primitives.begin(),image.primitives.end(),
            [&](const auto& row){return row.component==live.component && row.id==live.id && row.weak==live.weak;})!=image.primitives.end()) continue;
        ++born;
        if(born<=64) RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] lighting domain new id={} type={:x} name={} component={:x} mesh={:x} visibility={:x} render_flags={:x}\n"),
            live.id,live.proxy_type-base_,reinterpret_cast<RC::Unreal::UObject*>(live.component)->GetFullName(),live.component,
            live.proxy_inputs[0],live.proxy_inputs[5],live.proxy_inputs[6]);
    }
    RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] lighting domain inventory missing={} replaced={} new={} bounded_rows=64 writes=false\n"),missing,replaced,born);
    } catch(...) {} // Diagnostics cannot change the existing prepublication rejection.
}

bool Sc6ReplayParticleCopy::RebindLightingImage(LightingImage& image,const LightingImage& live_image) noexcept
{
    __try {
        if(!LightingBindings(live_image)) LIGHTING_REJECT();
        const bool preparing_target=&image==&lighting_a_ && !lighting_dirty_ && !lighting_executing_ && !lighting_transferred_;
        const auto deferred=[&](const LightingPrimitive& row) {
            return DormantRenderBinding(row) || (preparing_target && (PendingStageTargetBinding(row) || PendingTraceTargetBinding(row)));
        };
        unsigned changed{};
        // Native141490740 binds a scene-info to component ID+420, proxy+790
        // and scene+ D8. Scene-info+58 is its uniform publication. A component
        // GC lease cannot retain that proxy through native recreation. Resolve
        // only this operation's bindings from the freshly captured live image;
        // the immutable checkpoint, retained values and private undo remain untouched.
        for(const auto& saved:image.primitives) {
            const auto live=std::find_if(live_image.primitives.begin(),live_image.primitives.end(),
                [&](const auto& row){return row.component==saved.component && row.id==saved.id && row.weak==saved.weak;});
            if(live==live_image.primitives.end()) {
                if(deferred(saved)) {
                    ++changed;
                    continue;
                }
                DescribeLightingDomainDelta(image,live_image);
                const auto same_component=std::find_if(live_image.primitives.begin(),live_image.primitives.end(),
                    [&](const auto& row){return row.component==saved.component && row.weak==saved.weak;});
                const auto resolved=reinterpret_cast<void*(*)(const void*)>(base_+0xf823f0)(saved.weak.data());
                const bool alive=resolved==reinterpret_cast<void*>(saved.component);
                if(alive && preparing_target)for(const auto& plan:stage_render_owners_)
                    for(unsigned i=0;i<plan.original.component_count;++i)if(plan.original.components[i].object==saved.component) {
                        const auto& c=plan.original.components[i];
                        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] stage target deferral rejected id={} B_visibility={:x} A_visibility={:x} live_visibility={:x} B_flags={:x} live_flags={:x} asset_offset={:x} kind={} undo_ready={}\n"),
                            saved.id,c.visibility,plan.target_visibility[i],Field<unsigned>(saved.component,0x240),c.flags,
                            Field<unsigned>(saved.component,0x188),c.asset_offset,static_cast<unsigned>(plan.original.kind),witness_.undo_ready);
                    }
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] lighting missing target primitive component={:x} captured_id={} live_id={} member_same_generation={} weak_alive={} captured_type={:x} live_proxy={:x} name_index={} name_number={} slots={}\n"),
                    saved.component,saved.id,alive?Field<unsigned>(saved.component,0x420):0,
                    same_component!=live_image.primitives.end(),alive,saved.proxy_type-base_,
                    alive?Field<std::uintptr_t>(saved.component,0x790):0,
                    alive?Field<unsigned>(saved.component,0x18):0,alive?Field<unsigned>(saved.component,0x1c):0,saved.slot_count);
                LIGHTING_REJECT();
            }
            if(live->proxy==saved.proxy && live->address==saved.address) continue;
            // Base-only proxies map by component generation. The verified
            // static-mesh implementation additionally maps each LOD by its
            // retained mesh/render-data/light-map keys, never address order.
            const bool base_only=saved.slot_count==1 && live->slot_count==1;
            const bool lod_bound=saved.proxy_type==base_+0x36bd1a8
                && live->slot_count==saved.slot_count
                && saved.lci_binding.Matches(live->lci_binding,saved.slot_count);
            if(!saved.proxy_type || live->proxy_type!=saved.proxy_type
                || (!base_only && !lod_bound)
                || (saved.binding==LightingPrimitive::Binding::ScenePrimitive && saved.slots[0]!=saved.address+0x58)
                || live->slots[0]!=live->address+0x58) {
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] lighting rebind unsupported id={} slots={}/{} type={:x}/{:x}\n"),
                    saved.id,saved.slot_count,live->slot_count,saved.proxy_type-base_,live->proxy_type-base_);
                LIGHTING_REJECT();
            }
            for(unsigned slot=0;slot<saved.slot_count;++slot)
                if(std::count_if(image.uniforms.begin(),image.uniforms.end(),
                    [&](const auto& uniform){return uniform.component==saved.component && uniform.ordinal==slot;})!=1) LIGHTING_REJECT();
            ++changed;
        }
        if(!changed) return true;
        // Remap using the old primitive rows until every uniform is translated;
        // this also handles native address reuse between different proxies.
        for(auto& uniform:image.uniforms) {
            const auto saved=std::find_if(image.primitives.begin(),image.primitives.end(),
                [&](const auto& row){return row.component==uniform.component;});
            if(saved==image.primitives.end() || uniform.ordinal>=saved->slot_count) LIGHTING_REJECT();
            const auto live=std::find_if(live_image.primitives.begin(),live_image.primitives.end(),
                [&](const auto& row){return row.component==saved->component && row.id==saved->id && row.weak==saved->weak;});
            if(live==live_image.primitives.end()) {
                if(!deferred(*saved)) LIGHTING_REJECT();
                uniform.slot=0; // Keep the leased value; never follow an expired destination.
            } else uniform.slot=live->slots[uniform.ordinal];
        }
        for(auto& saved:image.primitives) {
            const auto live=std::find_if(live_image.primitives.begin(),live_image.primitives.end(),
                [&](const auto& row){return row.component==saved.component && row.id==saved.id && row.weak==saved.weak;});
            if(live==live_image.primitives.end()) {
                if(!deferred(saved)) LIGHTING_REJECT();
                saved.binding=preparing_target && PendingParticleTargetBinding(saved)?LightingPrimitive::Binding::PendingParticleTarget:
                    preparing_target && PendingTraceTargetBinding(saved)?LightingPrimitive::Binding::PendingTraceTarget:
                    preparing_target && PendingStageTargetBinding(saved)?LightingPrimitive::Binding::PendingStageTarget:
                    StageRenderBinding(saved)?LightingPrimitive::Binding::DormantStage:
                    TraceRenderBinding(saved)?LightingPrimitive::Binding::DormantTrace:LightingPrimitive::Binding::DormantCreation;
                saved.address=saved.proxy=0;saved.slots={};
                continue;
            }
            saved.binding=LightingPrimitive::Binding::ScenePrimitive;
            if(live->proxy==saved.proxy && live->address==saved.address) continue;
            saved.proxy=live->proxy;saved.address=live->address;saved.slots=live->slots;
        }
        for(std::size_t i=0;i<image.uniforms.size();++i)
            for(std::size_t j=0;j<i;++j)
                if(image.uniforms[i].slot && image.uniforms[i].slot==image.uniforms[j].slot) LIGHTING_REJECT();
        if(!LightingBindings(image)) LIGHTING_REJECT();
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] lighting bindings reconstructed count={} semantic_lod_mapping=true complete_B_retained=true\n"),changed);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {LIGHTING_REJECT();}
}

bool Sc6ReplayParticleCopy::PrepareLighting(std::size_t budget) noexcept
{
    __try {
        if(!RebindLighting()) LIGHTING_REJECT();
        if(!LightingBindings(lighting_a_) || !LightingBindings(lighting_b_) || lighting_dirty_ || lighting_transferred_) LIGHTING_REJECT();
        // Displaced B backing becomes replay-owned throughout undo, in
        // addition to its serialized image and A's new native allocations.
        std::size_t retained{};
        const auto quantify=reinterpret_cast<std::size_t(*)(std::size_t,unsigned)>(base_+0xd50dc0);
        for(const auto& row:lighting_b_.allocations) retained+=quantify(0x190,0);
        for(const auto& map:lighting_b_.maps) {
            const auto h=reinterpret_cast<std::uintptr_t>(map.header.data());
            if(!map.entries.empty()) retained+=quantify(map.entries.size(),0);
            if(Field<void*>(h,0x20)) retained+=quantify(map.flags.size(),0);
            if(Field<void*>(h,0x40)) retained+=quantify(map.hashes.size(),0);
        }
        if(witness_.bytes>budget || retained>budget-witness_.bytes) LIGHTING_REJECT();
        witness_.bytes+=retained;
        const auto allocate=[&](std::size_t bytes)->void* {
            if(!bytes) return nullptr;
            const auto charged=reinterpret_cast<std::size_t(*)(std::size_t,unsigned)>(base_+0xd50dc0)(bytes,0);
            if(charged<bytes || witness_.bytes>budget || charged>budget-witness_.bytes) return nullptr;
            witness_.bytes+=charged;
            return reinterpret_cast<void*(*)(std::size_t)>(base_+0x4a61c0)(bytes);
        };
        for(auto& row:lighting_a_.allocations) {
            if(row.installed) LIGHTING_REJECT();
            row.installed=allocate(0x190);
            if(!row.installed) LIGHTING_REJECT();
            std::memset(row.installed,0,0x190);std::memcpy(row.installed,row.values.data(),row.values.size());
        }
        auto& map=lighting_a_.maps[1];
        const auto h=reinterpret_cast<std::uintptr_t>(map.header.data());
        for(int i=0;i<Field<int>(h,8);++i) {
            if(!(Field<unsigned>(reinterpret_cast<std::uintptr_t>(map.flags.data()),std::size_t(i/32)*4)&(1u<<(i%32)))) continue;
            auto& pointer=Field<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(map.entries.data()),std::size_t(i)*0x18+8);
            const auto found=std::find_if(lighting_a_.allocations.begin(),lighting_a_.allocations.end(),[&](const auto& row){return row.address==pointer;});
            if(found==lighting_a_.allocations.end()) LIGHTING_REJECT();
            pointer=reinterpret_cast<std::uintptr_t>(found->installed);
        }
        for(auto& image:lighting_a_.maps) {
            const auto header=reinterpret_cast<std::uintptr_t>(image.header.data());
            const std::array<std::size_t,3> sizes{image.entries.size(),Field<void*>(header,0x20)?image.flags.size():0,Field<void*>(header,0x40)?image.hashes.size():0};
            const std::array<const void*,3> sources{image.entries.data(),image.flags.data(),image.hashes.data()};
            for(unsigned i=0;i<3;++i) {
                if(!sizes[i]) continue;
                if(image.installed[i]) LIGHTING_REJECT();
                image.installed[i]=allocate(sizes[i]);
                if(!image.installed[i]) LIGHTING_REJECT();
                std::memcpy(image.installed[i],sources[i],sizes[i]);
            }
        }
        return PublishLighting(true,true);
    } __except(EXCEPTION_EXECUTE_HANDLER) {LIGHTING_REJECT();}
}

bool Sc6ReplayParticleCopy::PublishLighting(bool original,bool preflight_only) noexcept
{
    if(lighting_executing_ && !lighting_execution_settled_) return false;
    __try {
        if(!LightingBindings(lighting_b_) || (original && !LightingBindings(lighting_a_))) LIGHTING_REJECT();
        const auto cache=scene_+0xda8;
        if(!VisibilityWritable(cache+0x50,0x50) || !VisibilityWritable(cache+0x140,0x50)
            || !VisibilityWritable(cache+0x138,4) || !VisibilityWritable(cache+0x30,1)) LIGHTING_REJECT();
        if(!original && !lighting_dirty_) return LightingImageMatches(false);
        if(original) {
            if(lighting_dirty_ || lighting_transferred_) LIGHTING_REJECT();
            for(unsigned i=0;i<2;++i) if(!LightingMapMatches(lighting_b_.maps[i],cache+(i?0x140:0x50),false)) LIGHTING_REJECT();
            for(const auto& row:lighting_b_.allocations)
                if(std::memcmp(row.values.data(),reinterpret_cast<void*>(row.address),row.values.size())) LIGHTING_REJECT();
            for(const auto& row:lighting_b_.uniforms) if(row.slot && Field<void*>(row.slot)!=row.value) LIGHTING_REJECT();
            for(const auto& row:lighting_b_.primitives)
                if(row.binding==LightingPrimitive::Binding::ScenePrimitive)
                if(Field<std::uintptr_t>(row.address,0x50)!=row.allocation || Field<unsigned char>(row.address,0xf2)!=row.dirty) LIGHTING_REJECT();
            if(preflight_only) return true;
            lighting_dirty_=true;
        }
        const auto& image=original?lighting_a_:lighting_b_;
        const auto assign=reinterpret_cast<void**(*)(void**,void*)>(base_+0x15b83f0);
        // Every B publication slot is restored or cleared according to A.
        // B-only entries stay retained for undo; they are not forgotten.
        const auto& destinations=lighting_executing_?lighting_a_:lighting_b_;
        for(const auto& row:destinations.uniforms) {
            if(!row.slot) continue;
            auto found=std::find_if(image.uniforms.begin(),image.uniforms.end(),[&](const auto& saved){return saved.slot==row.slot;});
            assign(reinterpret_cast<void**>(row.slot),found==image.uniforms.end()?nullptr:found->value);
        }
        for(const auto& row:destinations.primitives) {
            if(row.binding!=LightingPrimitive::Binding::ScenePrimitive) continue;
            const auto found=std::find_if(image.primitives.begin(),image.primitives.end(),[&](const auto& saved){return saved.address==row.address;});
            std::uintptr_t allocation{};std::uint8_t dirty{};
            if(found!=image.primitives.end()) {
                allocation=found->allocation;dirty=found->dirty;
                if(original && allocation) {
                    const auto value=std::find_if(image.allocations.begin(),image.allocations.end(),[&](const auto& saved){return saved.address==allocation;});
                    if(value==image.allocations.end() || !value->installed) LIGHTING_REJECT();
                    allocation=reinterpret_cast<std::uintptr_t>(value->installed);
                }
            }
            Field<std::uintptr_t>(row.address,0x50)=allocation;Field<std::uint8_t>(row.address,0xf2)=dirty;
        }
        for(unsigned i=0;i<2;++i) {
            auto header=image.maps[i].header;
            const auto h=reinterpret_cast<std::uintptr_t>(header.data());
            if(original) for(unsigned j=0;j<3;++j) if(image.maps[i].installed[j]) Field<void*>(h,j==0?0:j==1?0x20:0x40)=image.maps[i].installed[j];
            std::memcpy(reinterpret_cast<void*>(cache+(i?0x140:0x50)),header.data(),header.size());
            if(!LightingMapMatches(image.maps[i],cache+(i?0x140:0x50),original)) LIGHTING_REJECT();
        }
        Field<unsigned>(cache,0x138)=image.next_point;Field<std::uint8_t>(cache,0x30)=image.update_all;
        for(const auto& row:image.allocations)
            if(std::memcmp(row.values.data(),original?row.installed:reinterpret_cast<void*>(row.address),row.values.size())) LIGHTING_REJECT();
        for(const auto& row:image.uniforms) if(row.slot && Field<void*>(row.slot)!=row.value) LIGHTING_REJECT();
        if(!LightingImageMatches(original)) LIGHTING_REJECT();
        if(!original) lighting_dirty_=false;
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] lighting published target={} allocations={} next_point={} verified=true\n"),original,image.allocations.size(),image.next_point);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {LIGHTING_REJECT();}
}

bool Sc6ReplayParticleCopy::FinishLighting(bool commit) noexcept
{
    if(capture_owner_fault_)return false;
    if(lighting_executing_ && !lighting_execution_settled_) return false;
    __try {
        const auto free=reinterpret_cast<void(*)(void*)>(base_+0xd46a00);
        if(commit) {
            if(lighting_transferred_) return true;
            if(!lighting_dirty_ || !LightingImageMatches(true)) LIGHTING_REJECT();
            for(unsigned i=0;i<2;++i) if(!LightingMapMatches(lighting_a_.maps[i],scene_+0xda8+(i?0x140:0x50),true)) LIGHTING_REJECT();
            // All primitive bindings have moved off B. Native GPU uniform
            // references remain leased, and the enclosing GPU event retired.
            for(const auto& row:lighting_a_.primitives) {
                if(row.binding!=LightingPrimitive::Binding::ScenePrimitive) continue;
                const auto pointer=Field<std::uintptr_t>(row.address,0x50);
                for(const auto& allocation:lighting_b_.allocations) if(pointer==allocation.address) LIGHTING_REJECT();
            }
            for(auto& row:lighting_b_.allocations) {free(reinterpret_cast<void*>(row.address));row.address=0;}
            for(auto& map:lighting_b_.maps) {
                const auto h=reinterpret_cast<std::uintptr_t>(map.header.data());
                for(const auto offset:{0u,0x20u,0x40u}) if(auto* pointer=Field<void*>(h,offset)) {free(pointer);Field<void*>(h,offset)=nullptr;}
            }
            for(auto& row:lighting_a_.allocations) row.installed=nullptr;
            for(auto& map:lighting_a_.maps) map.installed={};
            lighting_dirty_=false;lighting_transferred_=true;
            return true;
        }
        if(lighting_dirty_) LIGHTING_REJECT();
        // Native B recreation may have new publication addresses. Before C
        // backing is freed, inspect the actual scene, not historical B proxy
        // addresses. No live primitive may still borrow a C point allocation.
        bool has_installed=false;
        for(const auto& row:lighting_a_.allocations)has_installed|=row.installed!=nullptr;
        // Empty or preflight-rejected operations own no installed point
        // allocation and may never have bound a scene. Only installed storage
        // requires the native non-borrowing census before it can be freed.
        if(has_installed) {
            if(!scene_) LIGHTING_REJECT();
            const auto count=Field<int>(scene_,0xad0);
            const auto primitives=Field<std::uintptr_t>(scene_,0xac8);
            if(count<0 || count>1024 || (count && !primitives)) LIGHTING_REJECT();
            for(int i=0;i<count;++i) {
                const auto primitive=Field<std::uintptr_t>(primitives,std::size_t(i)*8);
                if(!primitive || Field<std::uintptr_t>(primitive,0xd8)!=scene_ || Field<int>(primitive,0xe4)!=i) LIGHTING_REJECT();
                const auto allocation=Field<std::uintptr_t>(primitive,0x50);
                for(const auto& row:lighting_a_.allocations)
                    if(row.installed && allocation==reinterpret_cast<std::uintptr_t>(row.installed)) LIGHTING_REJECT();
            }
        }
        for(auto& row:lighting_a_.allocations) if(row.installed) {free(row.installed);row.installed=nullptr;}
        for(auto& map:lighting_a_.maps) for(auto*& pointer:map.installed) if(pointer) {free(pointer);pointer=nullptr;}
        const auto release=reinterpret_cast<void(*)(void**)>(base_+0x11b9260);
        // 1411B9260 is a destructor-style release: it leaves the slot intact.
        // Record each completed decrement before another participant can fail.
        for(auto* image:{&lighting_a_,&lighting_b_}) for(auto& row:image->uniforms) if(row.value) {release(&row.value);row.value=nullptr;}
        creation_render_owners_={};creation_render_owner_count_=0;
        stage_render_owners_.clear();
        particle_render_owners_={};particle_render_owner_count_=0;particle_render_bound_=false;
        trace_render_owner_=nullptr;
        fresh_trace_render_proof_={};fresh_trace_render_owners_={};fresh_trace_render_count_=0;
        ground_render_owner_=nullptr;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {capture_owner_fault_=true;LIGHTING_REJECT();}
}

bool Sc6ReplayParticleCopy::LightingPrivateUndoMatches(bool require_bindings) const noexcept
{
    __try {
        if(!lighting_b_.captured || (require_bindings && !LightingBindings(lighting_b_))) return false;
        for(const auto& map:lighting_b_.maps)
            if(!LightingMapMatches(map,reinterpret_cast<std::uintptr_t>(map.header.data()),false)) return false;
        for(const auto& row:lighting_b_.allocations)
            if(!row.address || std::memcmp(row.values.data(),reinterpret_cast<void*>(row.address),row.values.size())) return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}







bool Sc6ReplayParticleCopy::LightingExecutionDisjoint() const noexcept
{
    struct Range { std::uintptr_t address;std::size_t bytes; };
    const auto overlap=[](Range a,Range b) {
        if(!a.bytes || !b.bytes) return false;
        if(!a.address || !b.address || a.bytes>UINTPTR_MAX-a.address || b.bytes>UINTPTR_MAX-b.address) return true;
        return a.address<b.address+b.bytes && b.address<a.address+a.bytes;
    };
    __try {
        // Native capture bounds each image to 128 point allocations and six
        // map allocations. Stack metadata avoids another fallible allocation.
        std::array<Range,134> b{},c{};std::size_t bn{},cn{};
        const auto collect=[](const LightingImage& image,auto& ranges,std::size_t& count) {
            for(const auto& row:image.allocations) {
                if(count==ranges.size()) return false;
                ranges[count++]={row.address,row.values.size()};
            }
            for(const auto& map:image.maps) {
                const auto h=reinterpret_cast<std::uintptr_t>(map.header.data());
                const std::array<std::size_t,3> sizes{map.entries.size(),map.flags.size(),map.hashes.size()};
                for(unsigned i=0;i<3;++i) if(const auto address=Field<std::uintptr_t>(h,i==0?0:i==1?0x20:0x40)) {
                    if(count==ranges.size() || !sizes[i]) return false;
                    ranges[count++]={address,sizes[i]};
                }
            }
            return true;
        };
        if(!collect(lighting_b_,b,bn) || !collect(lighting_c_,c,cn)) return false;
        for(std::size_t i=0;i<cn;++i) {
            if(overlap(c[i],{scene_+0xda8,0x190})) return false;
            for(std::size_t j=0;j<bn;++j) if(overlap(c[i],b[j])) return false;
            for(std::size_t j=0;j<i;++j) if(overlap(c[i],c[j])) return false;
            for(const auto& row:lighting_b_.primitives) {
                if(row.binding!=LightingPrimitive::Binding::ScenePrimitive) continue;
                if(overlap(c[i],{row.address,0x100})) return false;
                for(unsigned slot=0;slot<row.slot_count;++slot)
                    if(overlap(c[i],{row.slots[slot],sizeof(void*)})) return false;
            }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool Sc6ReplayParticleCopy::SettleLightingExecution(std::size_t budget, RestoreSettlement purpose) noexcept
{
    if(!lighting_executing_ || lighting_execution_settled_ || !lighting_dirty_ || lighting_transferred_
        || !LightingPrivateUndoMatches(false)) return false;
    // A failed capture owns any references it acquired in lighting_c_. Keep
    // them for explicit failure retirement; do not reset a partially leased
    // image or blindly retry capture over its storage.
    if(!lighting_c_.captured && !CaptureLighting(lighting_c_,budget)) return false;
    // Private B allocations and uniform references have not been installed
    // during C. Re-resolve only their publication destinations after native
    // proxy recreation. This never dereferences the expired B proxy or alters
    // its saved values. Complete generation/ID/type/base-slot preflight precedes
    // every metadata change; subclass LCI relocation remains rejected.
    if(!LightingBindings(lighting_c_)) LIGHTING_REJECT();
    if(!RebindLightingImage(lighting_b_,lighting_c_)) LIGHTING_REJECT();
    unsigned deferred_static{},deferred_particle{};
    for(const auto& row:lighting_b_.primitives)if(row.binding==LightingPrimitive::Binding::DormantStage) {
        if(row.proxy_type==base_+0x36bd1a8)++deferred_static;else ++deferred_particle;
    }
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] lighting settlement stage_static_deferred={} stage_particle_deferred={} complete_B_retained=true\n"),deferred_static,deferred_particle);
    if(!LightingExecutionDisjoint()) LIGHTING_REJECT();
    if(!LightingPrivateUndoMatches()) LIGHTING_REJECT();
    if(purpose==RestoreSettlement::RecoverOriginal) for(const auto& c:lighting_c_.primitives) {
        const auto b=std::find_if(lighting_b_.primitives.begin(),lighting_b_.primitives.end(),
            [&](const auto& row){return row.component==c.component && row.weak==c.weak && row.id==c.id;});
        if(b==lighting_b_.primitives.end() && !DormantCreationBinding(c,false) && !StageRenderBinding(c,false) && !TraceRenderBinding(c,false)) {
            DescribeLightingDomainDelta(lighting_b_,lighting_c_);
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] lighting recovery unexpected C owner id={} type={:x} component={:x}\n"),c.id,c.proxy_type-base_,c.component);
            LIGHTING_REJECT();
        }
    }
    // C-only primitives must have completed native teardown before settlement.
    // B proxies remain live under quarantine; their existing checks still apply.
    for(const auto& b:lighting_b_.primitives) {
        if(b.binding!=LightingPrimitive::Binding::ScenePrimitive) continue;
        const auto c=std::find_if(lighting_c_.primitives.begin(),lighting_c_.primitives.end(),
            [&](const auto& row){return row.address==b.address;});
        if(c==lighting_c_.primitives.end() || b.component!=c->component || b.proxy!=c->proxy
            || b.weak!=c->weak || b.id!=c->id || b.slot_count!=c->slot_count || b.slots!=c->slots) LIGHTING_REJECT();
    }
    __try {
        const auto release=reinterpret_cast<void(*)(void**)>(base_+0x11b9260);
        for(auto& row:lighting_a_.uniforms) if(row.value) {release(&row.value);row.value=nullptr;}
    } __except(EXCEPTION_EXECUTE_HANDLER) {capture_owner_fault_=true;return false;}
    // Adopt the actual C addresses only after complete C/B disjointness. The
    // normal undo writer now restores B, and normal retirement frees C once.
    lighting_a_=std::move(lighting_c_);
    lighting_c_.captured=false;
    for(auto& row:lighting_a_.allocations) row.installed=reinterpret_cast<void*>(row.address);
    for(auto& map:lighting_a_.maps) {
        const auto h=reinterpret_cast<std::uintptr_t>(map.header.data());
        for(unsigned i=0;i<3;++i) map.installed[i]=Field<void*>(h,i==0?0:i==1?0x20:0x40);
    }
    lighting_execution_settled_=true;
    return LightingImageMatches(true);
}

#undef LIGHTING_REJECT
