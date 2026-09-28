struct ChildProjectionContext {
    const State* original{};const FreshChild* fresh{};void* old_proxy{};void* new_proxy{};
    std::uintptr_t base{};
};
struct ChildProjectionRegion {
    enum class Kind {State,Actor,Mesh,Animation,Proxy,ArrayHeader,ArrayPayload};
    std::uintptr_t source{},destination{};std::size_t bytes{};Kind kind{};
    const ChildProjectionContext* context{};
};
static bool ReadProjectedBinding(void* source,void* destination,std::size_t bytes) noexcept {
    __try {std::memcpy(destination,source,bytes);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
static bool ProjectChildBinding(const ChildProjectionRegion& region,const Image& source,Image& output) {
    using Kind=ChildProjectionRegion::Kind;
    const auto offset=reinterpret_cast<std::uintptr_t>(source.address)-region.source;
    if(source.bytes.size()>0x58)return false;
    std::array<std::byte,0x58> native{};
    if(!ReadProjectedBinding(output.address,native.data(),source.bytes.size()))return false;
    const auto equal=[&]{return !std::memcmp(source.bytes.data(),native.data(),source.bytes.size());};
    const auto pointer=[&](const void* old,const void* fresh) {
        return source.bytes.size()==8 && At<const void*>(const_cast<std::byte*>(source.bytes.data()),0)==old
            && At<const void*>(native.data(),0)==fresh;
    };
    const auto& old=*region.context->original;const auto& fresh=region.context->fresh->state;
    bool accepted{};
    if(source.mutable_bits) {
        if(source.bytes.size()>4)return false;
        const bool actor=region.kind==Kind::Actor && offset==0x85 && source.bytes.size()==1 && source.mutable_bits==0x80;
        const bool mesh=region.kind==Kind::Mesh && source.bytes.size()==4
            && ((offset==0x188 && source.mutable_bits==0x20040000) || (offset==0x240 && source.mutable_bits==0x11));
        unsigned a{},b{};std::memcpy(&a,source.bytes.data(),source.bytes.size());std::memcpy(&b,native.data(),source.bytes.size());
        accepted=(actor||mesh) && !((a^b)&~source.mutable_bits);
    } else switch(region.kind) {
    case Kind::State:
        if(offset==8)accepted=pointer(old.actor.object,fresh.actor.object);
        else if((offset==0x58 && source.bytes.size()==24) || (offset==0xa0 && source.bytes.size()==16)
            || (offset==0xb8 && source.bytes.size()==8))accepted=equal();
        break;
    case Kind::Actor:
        if(offset==0x168 || offset==0x398)accepted=pointer(old.mesh.object,fresh.mesh.object);
        break;
    case Kind::Mesh:
        if(offset==0x910 && source.bytes.size()==8)accepted=equal();
        else if(offset==0xab0)accepted=pointer(old.animation.object,fresh.animation.object);
        else if(offset==0x420 && source.bytes.size()==4)accepted=At<unsigned>(native.data(),0)!=UINT_MAX;
        else if(offset==0x1a8 && source.bytes.size()==32) {
            // Both attachment lists are empty at capture and on the fresh root.
            // Empty native backing/capacity belongs to its physical owner.
            accepted=!At<int>(const_cast<std::byte*>(source.bytes.data()),8)
                && !At<int>(const_cast<std::byte*>(source.bytes.data()),24)
                && !At<int>(native.data(),8) && !At<int>(native.data(),24);
        } else if(offset==0x1d0 && source.bytes.size()==16)accepted=equal();
        else if((offset==0x7b0 || offset==0xc58 || offset==0xe08) && source.bytes.size()==0x58) {
            // Only dormant secondary prefixes are captured here. Registered
            // ticks belong to scheduler projection and its backing journal.
            const auto* previous=At<Object*>(const_cast<std::byte*>(source.bytes.data()),0x50);
            const auto* target=At<Object*>(native.data(),0x50);
            // Primitive post-physics registration141DA9000 assigns Target only
            // when141D5FB60 admits CanEverTick. Its executor141D9BAB0 returns
            // immediately for null Target. Preserve the never-enabled null
            // pair; do not invent ownership or enable the tick.
            const bool null_primitive=offset==0x7b0 && !previous && !target
                && !(At<unsigned char>(native.data(),0xc)&2)
                && At<std::uintptr_t>(native.data(),0)==region.context->base+0x3882140;
            // Cloth registration141DB0560 additionally requires the native
            // mesh cloth policy; CanEverTick alone does not assign Target.
            // Its executor142100130 delegates to1420C0080, which guards null.
            const bool null_cloth=offset==0xc58 && !previous && !target
                && At<std::uintptr_t>(native.data(),0)==region.context->base+0x38829a8;
            // End-physics registration141DA9110 also assigns Target only on
            // native registration. Executor142100160/1420BFF90 guards null.
            const bool null_end_physics=offset==0xe08 && !previous && !target
                && At<std::uintptr_t>(native.data(),0)==region.context->base+0x3882990;
            accepted=!std::memcmp(source.bytes.data(),native.data(),0x50)
                && !(At<unsigned char>(native.data(),0xc)&0x40)
                && !At<void*>(native.data(),0x18)
                && (null_primitive || null_cloth || null_end_physics || (previous==old.mesh.object && target==fresh.mesh.object));
        }
        break;
    case Kind::Animation:
        if(offset==0x10 && source.bytes.size()==8)accepted=equal();
        else if(offset==0x350)accepted=pointer(region.context->old_proxy,region.context->new_proxy);
        break;
    case Kind::Proxy:
        if(offset==0xa0 && source.bytes.size()==16)accepted=
            At<void*>(const_cast<std::byte*>(source.bytes.data()),0)==old.animation.object
            && At<void*>(native.data(),0)==fresh.animation.object
            && At<void*>(const_cast<std::byte*>(source.bytes.data()),8)==At<void*>(native.data(),8);
        break;
    case Kind::ArrayHeader:
        if(offset==0 && source.bytes.size()==8)accepted=true; // Typed native array binding.
        else if(offset==12 && source.bytes.size()==4)accepted=equal();
        break;
    case Kind::ArrayPayload:break; // Captured trace rows contain values, not bindings.
    }
    if(!accepted) {
        std::array<std::uint64_t,2> saved_words{},native_words{};
        const auto n=(std::min)(source.bytes.size(),sizeof(saved_words));
        std::memcpy(saved_words.data(),source.bytes.data(),n);std::memcpy(native_words.data(),native.data(),n);
        std::size_t first{};while(first<source.bytes.size() && source.bytes[first]==native[first])++first;
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] trace child binding mismatch kind={} offset={:x} bytes={} mask={:x} first_difference={} saved={:x},{:x} native={:x},{:x} B_retained=true\n"),
            static_cast<unsigned>(region.kind),offset,source.bytes.size(),source.mutable_bits,first,
            saved_words[0],saved_words[1],native_words[0],native_words[1]);
        return false;
    }
    std::memcpy(output.bytes.data(),native.data(),source.bytes.size());return true;
}
Status ProjectFreshChildren(const Sc6ReplayTraceState& current,std::span<FreshChild* const> children,
    std::size_t budget,Sc6ReplayTraceState& output) const {
    using Kind=ChildProjectionRegion::Kind;
    if(!captured_ || thread_!=GetCurrentThreadId() || base_!=current.base_ || world_!=current.world_
        || children.empty() || children.size()>16 || output.base_ || output.captured_ || !output.roots_.empty())
        return Fail("child_projection_context");
    auto status=current.ValidateValues();if(!status.ok())return status;
    std::size_t region_count{};
    for(const auto* child:children) {
        if(!child || !child->prepared || child->retired || !child->identity_retained
            || child->history_phase!=FreshChild::HistoryPhase::Ready || child->animation_phase!=FreshChild::AnimationPhase::Ready
            || child->animation_arrays.empty() || child->animation_arrays.size()>257 || !child->lease.Validate().ok())
            return Fail("child_projection_owner");
        if(child->factory.phase!=Sc6ReplayTraceChildFactory::Phase::Returned
            || child->state.ref.state!=child->factory.reference.state || child->state.ref.controller!=child->factory.reference.controller
            || !child->state.ref.controller || child->state.ref.state!=child->state.ref.controller+16
            || At<std::uintptr_t>(child->state.ref.controller,0)!=base_+0x3362590
            || At<int>(child->state.ref.controller,8)!=1 || At<int>(child->state.ref.controller,12)<2)
            return Fail("child_projection_controller_identity");
        region_count+=11+child->animation_arrays.size()*2;
    }
    const auto scratch=region_count*sizeof(ChildProjectionRegion)+(roots_.size()*4+states_.size()*4)*sizeof(void*)
        +sizeof(ChildProjectionContext)*16;
    if(scratch>budget || owned_bytes()>budget-scratch)return Status::failure(FailureCode::CapacityExceeded);
    std::array<ChildProjectionContext,16> contexts{};
    std::vector<ChildProjectionRegion> regions;regions.reserve(region_count);
    const auto add=[&](void* old,void* fresh,std::size_t bytes,Kind kind,const ChildProjectionContext& context) {
        const auto a=reinterpret_cast<std::uintptr_t>(old),b=reinterpret_cast<std::uintptr_t>(fresh);
        if(!a || !b || !bytes || bytes>UINTPTR_MAX-a || bytes>UINTPTR_MAX-b || regions.size()==region_count)return false;
        regions.push_back({a,b,bytes,kind,&context});return true;
    };
    const auto array=[&](void* old,void* fresh,std::size_t stride,const ChildProjectionContext& context) {
        Array saved{};
        if(!RetainedField(bindings_,old,0,saved.data) || !RetainedField(bindings_,old,12,saved.capacity)
            || !RetainedField(values_,old,8,saved.count) || saved.count<0 || saved.capacity<saved.count || saved.capacity>1024)return false;
        const auto native=At<Array>(fresh,0);
        if(native.count!=saved.count || native.capacity!=saved.capacity || (native.capacity&&(!native.data||!saved.data)))return false;
        return add(old,fresh,16,Kind::ArrayHeader,context)
            && (!saved.capacity || add(saved.data,native.data,std::size_t(saved.capacity)*stride,Kind::ArrayPayload,context));
    };
    for(std::size_t i=0;i<children.size();++i) {
        auto& child=*children[i];auto& context=contexts[i];context.fresh=&child;context.base=base_;
        const auto old=std::find_if(states_.begin(),states_.end(),[&](const auto& s) {
            return s.ref.state==child.historical.state && s.ref.controller==child.historical.controller;
        });
        if(old==states_.end() || old->attachment.object || child.state.attachment.object
            || current.Contains(child.state.ref))return Fail("child_projection_source");
        for(std::size_t j=0;j<i;++j)if(contexts[j].original==&*old
            || contexts[j].fresh->state.ref.state==child.state.ref.state
            || contexts[j].fresh->state.actor==child.state.actor || contexts[j].fresh->state.mesh==child.state.mesh
            || contexts[j].fresh->state.animation==child.state.animation)return Fail("child_projection_duplicate_owner");
        context.original=&*old;
        unsigned source_flags{},source_visibility{},source_id{};
        const bool source_render_fields=RetainedField(values_,old->mesh.object,0x188,source_flags)
            && RetainedField(values_,old->mesh.object,0x240,source_visibility)
            && RetainedField(bindings_,old->mesh.object,0x420,source_id);
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] fresh trace retained render source child={} mesh={:x} fields={} flags={:x} visibility={:x} primitive_id={} historical_storage_read=false\n"),
            i,reinterpret_cast<std::uintptr_t>(old->mesh.object),source_render_fields,source_flags,source_visibility,source_id);
        if(!RetainedField(bindings_,old->animation.object,0x350,context.old_proxy))return Fail("child_projection_old_proxy");
        context.new_proxy=At<void*>(child.state.animation.object,0x350);
        if(!ValidatePreparedChildAnimation(child,*old))return Fail("child_projection_animation_owner");
        status=current.CheckChildStorageAndLifecycle(child.state,current,current);if(!status.ok())return status;
        if(!add(old->ref.state,child.state.ref.state,0xf8,Kind::State,context)
            || !add(old->actor.object,child.state.actor.object,0x410,Kind::Actor,context)
            || !add(old->mesh.object,child.state.mesh.object,0xe60,Kind::Mesh,context)
            || !add(old->animation.object,child.state.animation.object,0x400,Kind::Animation,context)
            || !add(context.old_proxy,context.new_proxy,0xb0,Kind::Proxy,context))return Fail("child_projection_region");
        for(const auto offset:{0x10u,0x70u,0x88u})
            if(!array(old->ref.state+offset,child.state.ref.state+offset,offset==0x10?0x70:0x2c,context))return Fail("child_projection_history_array");
        for(const auto& row:child.animation_arrays)
            if(!array(row.historical,row.destination,0x70,context))return Fail("child_projection_animation_array");
    }
    // Fresh destinations may coincide with expired A addresses. They may not
    // overlap any actually retained B field or native payload.
    for(const auto& region:regions)for(const auto* fields:{&current.values_,&current.bindings_})
        for(const auto& field:*fields)if(StorageOverlaps(reinterpret_cast<void*>(region.destination),region.bytes,field.address,field.bytes.size()))
            return Fail("child_projection_B_alias");
    for(const auto& region:regions)for(std::size_t i=0;i<current.dynamic_count_;++i) {
        const auto& image=current.dynamic_[i];
        if(StorageOverlaps(reinterpret_cast<void*>(region.destination),region.bytes,
            At<void*>(const_cast<std::byte*>(image.header.data()),0),image.bytes.size()))return Fail("child_projection_B_dynamic_alias");
    }
    try {
        output.base_=base_;output.world_=world_;output.thread_=thread_;output.budget_=budget-scratch;
        output.roots_=roots_;output.states_=states_;output.values_=values_;output.bindings_=bindings_;
        output.dynamic_=dynamic_;output.dynamic_count_=dynamic_count_;
        output.payload_bytes_=0;
        for(const auto* fields:{&output.values_,&output.bindings_})for(const auto& field:*fields)output.payload_bytes_+=field.bytes.capacity();
        if(output.owned_bytes()>output.budget_)return Status::failure(FailureCode::CapacityExceeded);
        for(auto& state:output.states_) {
            const auto mapped=std::find_if(contexts.begin(),contexts.begin()+children.size(),[&](const auto& c) {
                return c.original->ref.state==state.ref.state && c.original->ref.controller==state.ref.controller;
            });
            if(mapped!=contexts.begin()+children.size()) {state=mapped->fresh->state;continue;}
            const auto live=std::find_if(current.states_.begin(),current.states_.end(),[&](const auto& s) {
                return s.ref.state==state.ref.state && s.ref.controller==state.ref.controller;
            });
            if(live==current.states_.end() || live->actor!=state.actor || live->mesh!=state.mesh
                || live->animation!=state.animation || live->attachment!=state.attachment)return Fail("child_projection_common_owner");
        }
        const auto project=[&](const Image& source,Image& destination,bool binding) {
            const auto address=reinterpret_cast<std::uintptr_t>(source.address);
            if(source.bytes.size()>UINTPTR_MAX-address)return false;
            const ChildProjectionRegion* selected{};
            for(const auto& region:regions) {
                // A partially overlapping image is still an affected owner.
                // Leaving it at its historical address would bypass rebinding
                // and could later write a destroyed or reused allocation.
                if(address<region.source || address-region.source>region.bytes
                    || source.bytes.size()>region.bytes-(address-region.source)) {
                    if(source.bytes.size() && region.bytes && address<region.source+region.bytes
                        && region.source<address+source.bytes.size())return false;
                    continue;
                }
                if(selected && region.destination+(address-region.source)!=selected->destination+(address-selected->source))return false;
                if(!selected || region.bytes<selected->bytes)selected=&region;
            }
            if(!selected)return true; // Retained common owner or immutable metadata.
            destination.projected_child=true;
            destination.address=reinterpret_cast<std::byte*>(selected->destination+(address-selected->source));
            if(binding)return ProjectChildBinding(*selected,source,destination);
            const auto mask=std::find_if(bindings_.begin(),bindings_.end(),[&](const auto& b){return b.address==source.address && b.mutable_bits;});
            if(mask!=bindings_.end()) {
                if(source.bytes.size()>4 || source.bytes.size()!=mask->bytes.size())return false;
                unsigned saved{},native{};std::memcpy(&saved,source.bytes.data(),source.bytes.size());
                if(!ReadProjectedBinding(destination.address,&native,source.bytes.size()))return false;
                native=(native&~mask->mutable_bits)|(saved&mask->mutable_bits);
                std::memcpy(destination.bytes.data(),&native,source.bytes.size());
            }
            return true;
        };
        for(std::size_t i=0;i<bindings_.size();++i)if(!project(bindings_[i],output.bindings_[i],true)) {
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] trace child projection rejected binding={} source={:x} destination={:x} bytes={} mask={:x} B_retained=true\n"),
                i,reinterpret_cast<std::uintptr_t>(bindings_[i].address),reinterpret_cast<std::uintptr_t>(output.bindings_[i].address),
                bindings_[i].bytes.size(),bindings_[i].mutable_bits);
            return Fail("child_projection_binding");
        }
        for(std::size_t i=0;i<values_.size();++i)if(!project(values_[i],output.values_[i],false))return Fail("child_projection_value");
        for(std::size_t i=0;i<dynamic_count_;++i)if(output.dynamic_[i].array()) {
            auto& image=output.dynamic_[i];const auto array=At<Array>(image.header.data(),0);
            if(array.count<0 || std::size_t(array.count)>image.bytes.size()/16)return Fail("child_projection_reference_count");
            for(int n=0;n<array.count;++n) {
                auto& ref=At<Ref>(image.bytes.data(),std::size_t(n)*16);
                for(const auto* child:children) {
                    if(ref.state==child->historical.state || ref.controller==child->historical.controller) {
                        if(ref.state!=child->historical.state || ref.controller!=child->historical.controller)return Fail("child_projection_reference_pair");
                        ref=child->state.ref;break;
                    }
                }
            }
        }
        for(const auto& ref:expired_)if(!output.RetainExpired(ref))return Fail("child_projection_expired_identity");
        std::vector<void*> objects;objects.reserve(roots_.size()*4+states_.size()*4);
        for(const auto& root:output.roots_)for(auto* p:{root.component.object,root.chara.object,root.manager.object,root.scene.object})if(p)objects.push_back(p);
        for(const auto& state:output.states_)for(auto* p:{state.actor.object,state.mesh.object,state.animation.object,state.attachment.object})if(p)objects.push_back(p);
        if(output.owned_bytes()>output.budget_)return Status::failure(FailureCode::CapacityExceeded);
        status=output.lease_.Acquire(base_,objects,output.budget_-output.owned_bytes());if(!status.ok())return status;
        output.captured_=true;
        status=output.ValidateBindings();if(status.ok())status=current.ValidateValues();return status;
    } catch(...) {return Status::failure(FailureCode::CapacityExceeded);}
}
// A read-only proof view for owners that must survive corrected execution.
// Reconstructed children have their own strong/death journals. This subset
// must never be installed as simulation state or used as a complete checkpoint.
Status CreateSurvivingOwnerView(std::span<FreshChild* const> children,std::size_t budget,
    Sc6ReplayTraceState& output) const {
    if(validation_only_ || children.empty() || children.size()>16 || output.base_ || output.captured_ || !output.roots_.empty())
        return Fail("survivor_view_context");
    auto status=ValidateBindings();if(!status.ok())return status;
    const auto scratch=(roots_.size()*4+states_.size()*4)*sizeof(void*);
    if(scratch>budget || owned_bytes()>budget-scratch)return Status::failure(FailureCode::CapacityExceeded);
    try {
        std::array<bool,16> found{},strong{};
        const auto child_index=[&](const Ref& ref) {
            std::size_t result=children.size();
            for(std::size_t i=0;i<children.size();++i) {
                if(!children[i])return children.size()+1;
                const auto& owned=children[i]->state.ref;
                if(ref.state==owned.state || ref.controller==owned.controller) {
                    if(ref.state!=owned.state || ref.controller!=owned.controller || result!=children.size())return children.size()+1;
                    result=i;
                }
            }
            return result;
        };
        output.base_=base_;output.world_=world_;output.thread_=thread_;output.budget_=budget-scratch;
        output.validation_only_=true;output.survivor_source_=this;output.roots_=roots_;output.states_.reserve(states_.size());
        for(const auto& state:states_) {
            const auto index=child_index(state.ref);
            if(index>children.size())return Fail("survivor_view_identity");
            if(index==children.size())output.states_.push_back(state);
            else {
                const auto& child=*children[index];
                if(found[index] || !child.prepared || !child.identity_retained || child.retired
                    || child.history_phase!=FreshChild::HistoryPhase::Ready || child.animation_phase!=FreshChild::AnimationPhase::Ready
                    || state.actor!=child.state.actor || state.mesh!=child.state.mesh || state.animation!=child.state.animation
                    || state.attachment!=child.state.attachment || !child.lease.Validate().ok())return Fail("survivor_view_child");
                found[index]=true;
            }
        }
        for(std::size_t i=0;i<children.size();++i)if(!found[i])return Fail("survivor_view_child_missing");
        const auto copy_common=[&](const auto& source,auto& destination) {
            destination.reserve(source.size());
            for(const auto& image:source)if(!image.projected_child) {
                destination.push_back(image);output.payload_bytes_+=destination.back().bytes.capacity();
            }
        };
        copy_common(values_,output.values_);copy_common(bindings_,output.bindings_);
        output.dynamic_=dynamic_;output.dynamic_count_=dynamic_count_;
        for(std::size_t i=0;i<dynamic_count_;++i)if(dynamic_[i].kind==DynamicImage::Kind::ChildStrongReferences) {
            auto& image=output.dynamic_[i];const auto array=At<Array>(image.header.data(),0);
            if(array.count<0 || std::size_t(array.count)>image.bytes.size()/16)return Fail("survivor_view_strong_count");
            for(int n=0;n<array.count;++n) {
                const auto index=child_index(At<Ref>(image.bytes.data(),std::size_t(n)*16));
                if(index>=children.size() || strong[index])return Fail("survivor_view_strong_owner");
                strong[index]=true;
            }
            // This is the common ownership inventory, not a historical write.
            image.header={};std::vector<std::byte>().swap(image.bytes);
        }
        for(std::size_t i=0;i<children.size();++i)if(!strong[i])return Fail("survivor_view_strong_missing");
        if(output.owned_bytes()>output.budget_)return Status::failure(FailureCode::CapacityExceeded);
        for(const auto& ref:expired_)if(!output.RetainExpired(ref))return Fail("survivor_view_expired_identity");
        std::vector<void*> objects;objects.reserve(roots_.size()*4+output.states_.size()*4);
        for(const auto& root:output.roots_)for(auto* p:{root.component.object,root.chara.object,root.manager.object,root.scene.object})if(p)objects.push_back(p);
        for(const auto& state:output.states_)for(auto* p:{state.actor.object,state.mesh.object,state.animation.object,state.attachment.object})if(p)objects.push_back(p);
        if(output.owned_bytes()>output.budget_)return Status::failure(FailureCode::CapacityExceeded);
        status=output.lease_.Acquire(base_,objects,output.budget_-output.owned_bytes());if(!status.ok())return status;
        output.captured_=true;return output.ValidateBindings();
    } catch(...) {return Status::failure(FailureCode::CapacityExceeded);}
}
