// Native private acquisition only. Dynamic state and scene publication are
// separate typed operations; this never authorizes historical A publication.
namespace {
struct GroundPhysicsNative {
    using Owner=Sc6ReplayGroundDebrisState::ColdRoot;
    using Native=Sc6ReplayColdParticleOwner;
    static bool Identity(std::uintptr_t base) noexcept {
        struct Signature {unsigned offset;std::array<unsigned char,20> bytes;};
        constexpr Signature signatures[]{
            {0x1fe7680,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x41,0x56,0x41,0x57}},
            {0x1ff1790,{0x4c,0x89,0x4c,0x24,0x20,0x4c,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x55,0x53,0x56,0x57,0x48}},
            {0x2005c40,{0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x33,0xff,0x48,0x8b,0xd9,0x48,0x39,0xb9,0x10,0x02}},
            {0x2014460,{0x48,0x89,0x7c,0x24,0x20,0x55,0x48,0x8d,0x6c,0x24,0xc0,0x48,0x81,0xec,0x40,0x01,0x00,0x00,0x80,0xb9}}
            ,{0x2046020,{0x8b,0x05,0x62,0x00,0x05,0x02,0x45,0x33,0xd2,0x3b,0x05,0x85,0x00,0x05,0x02,0x4c,0x63,0xc9,0x74,0x64}}
        };
        __try {for(const auto& s:signatures)if(std::memcmp(reinterpret_cast<void*>(base+s.offset),s.bytes.data(),s.bytes.size()))return false;
            return true;} __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool Queues(const Owner& owner) noexcept {
        __try {
            if(Native::Read<std::uintptr_t>(owner.physics_world,0x1c8)!=owner.physics_owner)return false;
            std::array<ReplayGroundPrivateQueues::Binding,32> bindings{};unsigned count{};
            for(unsigned i=0;i<owner.child_count;++i)for(const auto& b:owner.children[i].private_bodies)
                if(b.actor)bindings[count++]={b.actor,b.body};
            const auto read=[](auto p,auto& v){v=Native::Read<std::remove_reference_t<decltype(v)>>(p);return true;};
            return ReplayGroundPrivateQueues::Absent(read,owner.physics_owner,std::span(bindings.data(),count));
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool Construct(Owner& owner,Owner::Child& child) noexcept {
        __try {
            owner.check="ground_physics_private_native_factory";
            const auto b=owner.base,body=child.object+0x430;
            auto& arrays=child.physics_arrays;
            child.physics_entered=true;
            arrays[0].data=GroundGraphNative::Allocate(b,8);
            arrays[1].data=GroundGraphNative::Allocate(b,0x30);
            if(!arrays[0].data || !arrays[1].data)return false;
            arrays[0].count=arrays[0].capacity=arrays[1].count=arrays[1].capacity=1;
            std::memcpy(reinterpret_cast<void*>(arrays[0].data),&body,8);
            std::memcpy(reinterpret_cast<void*>(arrays[1].data),child.captured_transform.data(),0x30);
            reinterpret_cast<void*(*)(void*,void*,void*,std::uintptr_t,std::uintptr_t,std::uintptr_t,void*,std::uintptr_t,std::uintptr_t)>(b+0x1fe7680)
                (child.physics_helper.data(),&arrays[0],&arrays[1],child.body_setup,child.object,owner.physics_owner,child.physics_options.data(),0,0);
            const auto read=[](auto p,auto& v){v=Native::Read<std::remove_reference_t<decltype(v)>>(p);return true;};
            const auto resolve=[&](std::int16_t id){return reinterpret_cast<std::uintptr_t(*)(std::int16_t)>(b+0x2046020)(id);};
            const auto helper=reinterpret_cast<std::uintptr_t>(child.physics_helper.data());
            owner.check="ground_physics_scene_identity";
            if(!child.private_scene.Capture(read,resolve,owner.physics_module,owner.physics_world,owner.physics_owner,helper))return false;
            owner.check="ground_physics_private_native_factory";
            unsigned char asynchronous{};
            if(!reinterpret_cast<unsigned char(*)(void*,void*,void*,void*,unsigned char,unsigned char*)>(b+0x1ff1790)
                (child.physics_helper.data(),&arrays[2],&arrays[3],&arrays[4],0,&asynchronous))return false;
            owner.check="ground_physics_private_native_bindings";
            // These are empty on the verified non-debug static-mesh route.
            // Never silently abandon an unexpected native debug owner.
            if(Native::Read<std::uintptr_t>(helper,0x38) || Native::Read<std::uintptr_t>(helper,0x48)
                || Native::Read<std::uintptr_t>(helper,0x50))return false;
            if(!child.private_scene.Validate(read,resolve))return false;
            const auto scene=[&](std::uintptr_t p){return reinterpret_cast<std::uintptr_t(*)(std::uintptr_t)>(owner.physics_module+0x3c8e0)(p);};
            unsigned actors{};
            for(unsigned side=0;side<2;++side) {
                const auto actor=Native::Read<std::uintptr_t>(body,0xf0+side*8);
                if(actor) {
                    ++actors;
                    if(!Native::Read<unsigned short>(body,0x10+side*2)
                        || !child.private_bodies[side].Capture(read,scene,actor,owner.physics_module,body)
                        || !child.private_scene.Matches(child.private_bodies[side]))return false;
                    owner.check="ground_physics_initial_state_install";
                    const auto write=[](auto p,const auto& v){std::memcpy(reinterpret_cast<void*>(p),&v,sizeof(v));return true;};
                    const auto values=[&](std::uintptr_t p,auto& output) {
                        reinterpret_cast<void*(*)(void*,std::uintptr_t)>(owner.physics_module+0x7810)(output.data(),p);return true;
                    };
                    if(!child.source_actors[side] || !Queues(owner)
                        || !child.initial_body_states[side].Install(read,write,scene,values,child.private_bodies[side],&owner.physics_state_offset,
                            {Native::Read<unsigned>(child.object,0xc),Native::Read<unsigned>(owner.outer,0xc)}))return false;
                    owner.check="ground_physics_private_native_bindings";
                }
                const auto& list=arrays[2+side];
                if(list.count!=(actor?1:0) || list.capacity<list.count || list.capacity>64
                    || (actor && (!list.data || Native::Read<std::uintptr_t>(list.data)!=actor)))return false;
            }
            if(actors!=1 || arrays[4].count!=1 || arrays[4].capacity<1 || arrays[4].capacity>64 || !arrays[4].data)return false;
            const auto dynamic=Native::Read<std::uintptr_t>(arrays[4].data);
            if(dynamic!=child.private_bodies[0].actor && dynamic!=child.private_bodies[1].actor)return false;
            if(Native::Read<std::array<int,2>>(body,0x138)!=child.weak
                || Native::Read<std::array<int,2>>(body,0x140)!=child.body_setup_weak
                || Native::Read<unsigned>(body,0x228)!=2 || Native::Read<std::uintptr_t>(body,0x100)
                || Native::Read<std::uintptr_t>(body,0x210))return false;
            // The helper sampled B's owning-actor velocity. Restore the owned
            // A runtime fields before any simulation-enable/impulse callback.
            std::memcpy(reinterpret_cast<void*>(body+0x12c),child.initial_body_velocity.data(),12);
            auto& flags=*reinterpret_cast<unsigned*>(body+0x74);
            flags=(flags&~0x40000u)|child.initial_body_velocity_flag;
            if(Native::Read<std::array<std::byte,12>>(body,0x12c)!=child.initial_body_velocity
                || (Native::Read<unsigned>(body,0x74)&0x40000u)!=child.initial_body_velocity_flag)return false;
            child.physics_ready=true;return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool Retire(Owner& owner) noexcept {
        if(!owner.physics_entered)return true;
        if(!owner.physics_ready || !Identity(owner.base) || !Queues(owner))return false;
        __try {
            for(unsigned i=0;i<owner.child_count;++i) {
                auto& child=owner.children[i];if(child.physics_retired)continue;
                if(!child.physics_ready || !GroundGraphNative::ChildPrivate(owner.base,child.object,owner.object,owner.outer,nullptr,nullptr,&child))return false;
                // Keep real scene identifiers until TermBody releases actors.
                reinterpret_cast<void(*)(std::uintptr_t)>(owner.base+0x2005c40)(child.object+0x430);
                if(Native::Read<std::uintptr_t>(child.object,0x520) || Native::Read<std::uintptr_t>(child.object,0x528)
                    || Native::Read<unsigned>(child.object,0x440) || Native::Read<unsigned>(child.object,0x658))return false;
                child.physics_retired=true;
                for(auto& array:child.physics_arrays) {
                    if(array.data && !GroundGraphNative::Free(owner.base,array.data))return false;
                    array={};
                }
            }
            return Queues(owner);
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
};
}
Status Sc6ReplayGroundDebrisState::ConstructColdPhysics(std::size_t budget,ColdRoot& output) const noexcept {
    using Native=Sc6ReplayColdParticleOwner;
    const auto fail=[&](FailureCode code,const char* why){output.check=why;return Status::failure(code);};
    if(!state_ || output.phase!=ColdRoot::Phase::Cold || !output.graph_ready || output.physics_entered
        || !GroundGraphNative::Private(output))return fail(FailureCode::IllegalTransition,"ground_physics_domain");
    if(state_->thread!=GetCurrentThreadId())return fail(FailureCode::WrongThread,"ground_physics_thread");
    constexpr std::size_t allowance=16*1024*1024;
    if(output.owned_bytes()>budget || allowance>budget-output.owned_bytes())return fail(FailureCode::CapacityExceeded,"ground_physics_budget");
    if(!GroundPhysicsNative::Identity(output.base) || !state_->configuration_lease || !state_->configuration_lease->Validate().ok())
        return fail(FailureCode::GenerationMismatch,"ground_physics_identity");
    output.physics_world=state_->world;
    if(!State::read(state_->world+0x1c8,output.physics_owner) || !output.physics_owner)
        return fail(FailureCode::ContextUnavailable,"ground_physics_owner");
    output.physics_module=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PhysX3_x64.dll"));
    if(!output.physics_module || !GroundPhysicsNative::Queues(output))return fail(FailureCode::UnsupportedContent,"ground_physics_queues");
    try {
        auto* lock=reinterpret_cast<LPCRITICAL_SECTION>(output.base+0x429f678);
        if(!TryEnterCriticalSection(lock))return fail(FailureCode::ContextUnavailable,"ground_physics_gc_busy");
        struct Unlock {LPCRITICAL_SECTION lock;~Unlock(){LeaveCriticalSection(lock);}} unlock{lock};
        if(Native::Read<int>(output.base,0x429f674) || Native::Read<int>(output.base,0x429fa54)
            || Native::Read<unsigned char>(output.base,0x429f648) || Native::Read<unsigned char>(output.base,0x429fa0c))
            return fail(FailureCode::ContextUnavailable,"ground_physics_gc_pending");
        for(unsigned i=0;i<output.child_count;++i) {
            auto& child=output.children[i];
            unsigned supported{};
            for(unsigned side=0;side<2;++side)if(child.source_actors[side]) {
                if(!child.initial_body_states[side].ready) {
                    output.child_observed=i;
                    return fail(FailureCode::UnsupportedContent,"ground_physics_initial_state_not_captured");
                }
                ++supported;
            }
            if(supported!=1)return fail(FailureCode::UnsupportedContent,"ground_physics_initial_actor_count");
            child.body_setup=Native::Read<std::uintptr_t>(child.asset,0x80);
            if(Native::Read<unsigned char>(child.object,0x318)!=2 || !child.body_setup
                || !Native::Live(reinterpret_cast<RC::Unreal::UObject*>(child.body_setup))
                || !State::bind(output.base,child.body_setup,child.body_setup_weak)
                || !Native::Read<unsigned char>(child.body_setup,0x390)
                || Native::Read<std::uintptr_t>(Native::Read<std::uintptr_t>(child.body_setup),0x230)!=output.base+0x2014460)
                return fail(FailureCode::UnsupportedContent,"ground_physics_ready_setup");
        }
        output.physics_entered=true;output.phase=ColdRoot::Phase::Entered;output.allowance+=allowance;
        for(unsigned i=0;i<output.child_count;++i) {
            output.child_observed=i;
            if(!GroundPhysicsNative::Construct(output,output.children[i]))return Status::failure(FailureCode::RestorePreflightFailed);
            if(!GroundPhysicsNative::Queues(output))return fail(FailureCode::RestorePreflightFailed,"ground_physics_private_consumers");
        }
        if(!GroundGraphNative::Private(output))return fail(FailureCode::RestorePreflightFailed,"ground_physics_private_graph");
        output.physics_ready=true;output.phase=ColdRoot::Phase::Cold;output.check="ground_physics_private_ready";
        return Status::success();
    } catch(...) {return fail(FailureCode::RestorePreflightFailed,"ground_physics_exception_hold_required");}
}

