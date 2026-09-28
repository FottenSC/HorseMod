// Included in Horse::Deterministic after cold-root acquisition.
namespace {
struct GroundGraphNative {
    using Owner=Sc6ReplayGroundDebrisState::ColdRoot;
    using Native=Sc6ReplayColdParticleOwner;
    struct Header {std::uintptr_t data{};int count{},capacity{};};
    static bool Identity(std::uintptr_t base) noexcept {
        struct Signature {unsigned offset;std::array<unsigned char,20> bytes;};
        constexpr Signature signatures[]{
            {0x863660, {0x4c,0x89,0x74,0x24,0x20,0x41,0x57,0x48,0x83,0xec,0x20,0x4c,0x8b,0xfa,0x4c,0x8b,0xf1,0x48,0x3b,0xca}},
            {0x4a61c0, {0x33,0xd2,0xe9,0x99,0x95,0x8a,0x0,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0x4c,0x89,0x74,0x24}},
            {0xd46a00, {0x48,0x85,0xc9,0x74,0x1d,0x4c,0x8b,0x05,0xbc,0x07,0x45,0x03,0x4d,0x85,0xc0,0x0f,0x84,0xbb,0x05,0x00}},
            {0x863b30, {0x48,0x89,0x5c,0x24,0x8,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xda,0x48,0x8b,0xf9,0xe8,0x1b,0xfb,0xff}},
            {0x1dd8790, {0x48,0x89,0x5c,0x24,0x18,0x57,0x48,0x81,0xec,0xf0,0x0,0x0,0x0,0x48,0x8b,0xfa,0x48,0x8b,0xd9,0x48}},
            {0x1dd0dc0, {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x48,0x83,0xec,0x20,0x33,0xdb,0x48,0x8b,0xea}},
            {0x1ec5e60, {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xc2,0x48,0x8b,0xd9,0x48,0x85,0xd2,0x74,0x1a,0x48,0x8b,0xd1}},
            {0x1d86410, {0x85,0xd2,0xf,0x88,0xd,0x1,0x0,0x0,0x48,0x89,0x5c,0x24,0x8,0x48,0x89,0x74,0x24,0x10,0x57,0x48}},
            {0x3078d00, {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x55,0x41,0x56,0x41,0x57}},
            {0x1daca60, {0x48,0x89,0x5c,0x24,0x8,0x57,0x48,0x83,0xec,0x30,0xf3,0xf,0x10,0xa,0x48,0x8b,0xfa,0xf,0x2e,0x89}},
            {0x1dae0a0, {0x40,0x53,0x55,0x56,0x57,0x48,0x81,0xec,0xa8,0x0,0x0,0x0,0x48,0x8b,0x5,0xad,0xb1,0x33,0x2,0x48}},
        };
        __try {
            for(const auto& s:signatures)if(std::memcmp(reinterpret_cast<void*>(base+s.offset),s.bytes.data(),s.bytes.size()))return false;
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static std::uintptr_t Allocate(std::uintptr_t base,std::size_t bytes) noexcept {
        __try {auto p=reinterpret_cast<std::uintptr_t(*)(std::size_t)>(base+0x4a61c0)(bytes);
            if(p)std::memset(reinterpret_cast<void*>(p),0,bytes);return p;
        } __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
    }
    static bool Configure(std::uintptr_t base,std::uintptr_t root,const void* image) noexcept {
        __try {
            reinterpret_cast<void(*)(void*,const void*)>(base+0x863b30)(reinterpret_cast<void*>(root+0x990),image);
            reinterpret_cast<void(*)(void*,const void*)>(base+0x863660)(reinterpret_cast<void*>(root+0x840),image);
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool Mesh(std::uintptr_t base,std::uintptr_t object,std::uintptr_t asset) noexcept {
        __try {return (reinterpret_cast<unsigned char(*)(void*,void*)>(base+0x1dd8790)
            (reinterpret_cast<void*>(object),reinterpret_cast<void*>(asset))&1)!=0;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool RemoveStreaming(std::uintptr_t base,std::uintptr_t object) noexcept {
        __try {
            const auto collection=Native::Read<std::uintptr_t>(base+0x43931d8);
            if(!collection || Native::Read<int>(collection,0x18)<1 || Native::Read<int>(collection,0x18)>8)return false;
            const auto entries=Native::Read<std::uintptr_t>(collection,0x10);if(!entries)return false;
            for(int i=0;i<Native::Read<int>(collection,0x18);++i) {
                const auto manager=Native::Read<std::uintptr_t>(entries,i*8);
                if(!manager)return false;
                const auto remove=Native::Read<std::uintptr_t>(Native::Read<std::uintptr_t>(manager),0x70);
                if(remove!=base+0x214b520 && remove!=base+0x2d2bc0)return false;
            }
            reinterpret_cast<void(*)(void*,void*)>(base+0x1dd0dc0)(reinterpret_cast<void*>(collection),reinterpret_cast<void*>(object));
            return !(Native::Read<unsigned>(object,0x3f8)&3);
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static std::uintptr_t Material(std::uintptr_t base,std::uintptr_t root,std::uintptr_t parent) noexcept {
        __try {return reinterpret_cast<std::uintptr_t(*)(void*,void*)>(base+0x1ec5e60)
            (reinterpret_cast<void*>(root),reinterpret_cast<void*>(parent));}
        __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
    }
    static bool Override(std::uintptr_t base,std::uintptr_t object,unsigned slot,std::uintptr_t material) noexcept {
        __try {reinterpret_cast<void(*)(void*,int,void*)>(base+0x1d86410)(reinterpret_cast<void*>(object),slot,reinterpret_cast<void*>(material));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool Bind(std::uintptr_t base,std::uintptr_t root,std::uintptr_t callback) noexcept {
        __try {reinterpret_cast<void(*)(void*,void*,void*)>(base+0x3078d00)
            (reinterpret_cast<void*>(root+0x890),reinterpret_cast<void*>(root+0x820),reinterpret_cast<void*>(callback));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool ChildPrivate(std::uintptr_t base,std::uintptr_t object,std::uintptr_t root,std::uintptr_t actor,
        const char** predicate=nullptr,std::uintptr_t* observed=nullptr,const Owner::Child* child=nullptr) noexcept {
        if(predicate)*predicate="ground_graph_child_cold";
        if(observed)*observed=0;
        // Failure-only witness: preserve the exact admission predicate and its
        // first failing native value, without a second read after acquisition.
#define GROUND_CHILD_FIELD(type,offset,mask,expected,label) \
        { const auto value=Native::Read<type>(object,offset); \
          if((std::uintptr_t(value)&std::uintptr_t(mask))!=std::uintptr_t(expected)) { \
            if(predicate)*predicate=label;if(observed)*observed=std::uintptr_t(value);return false; } }
        __try {
            GROUND_CHILD_FIELD(std::uintptr_t,0,~std::uintptr_t{},base+0x36cefb0,"ground_child_vtable");
            GROUND_CHILD_FIELD(std::uintptr_t,0x20,~std::uintptr_t{},root,"ground_child_outer");
            GROUND_CHILD_FIELD(std::uintptr_t,0x190,~std::uintptr_t{},actor,"ground_child_actor");
            //141D6C770 sets 0x200000 in the unregistered mesh constructor.
            // Preserve that navigation capability; registration/body/render
            // ownership is admitted independently, never by clearing flags.
            GROUND_CHILD_FIELD(unsigned,0x188,1u|2u|4u|0x40000u|0x1c000000u|0x20000000u,0,"ground_child_flags");
            GROUND_CHILD_FIELD(std::uintptr_t,0x1c8,~std::uintptr_t{},0,"ground_child_registered_world");
            GROUND_CHILD_FIELD(int,0x6a8,~std::uintptr_t{},0,"ground_child_overlaps");
            GROUND_CHILD_FIELD(int,0x3d8,~std::uintptr_t{},0,"ground_child_move_scopes");
            GROUND_CHILD_FIELD(int,0x3c4,~std::uintptr_t{},0,"ground_child_scoped_updates");
            GROUND_CHILD_FIELD(unsigned char,0x11c,0x40,0,"ground_child_primary_tick");
            GROUND_CHILD_FIELD(unsigned char,0x7bc,0x40,0,"ground_child_secondary_tick");
            GROUND_CHILD_FIELD(std::uintptr_t,0x128,~std::uintptr_t{},0,"ground_child_primary_task");
            GROUND_CHILD_FIELD(std::uintptr_t,0x7c8,~std::uintptr_t{},0,"ground_child_secondary_task");
            GROUND_CHILD_FIELD(std::uintptr_t,0x1d0,~std::uintptr_t{},0,"ground_child_attachment");
            GROUND_CHILD_FIELD(int,0x1e0,~std::uintptr_t{},0,"ground_child_attach_children");
            GROUND_CHILD_FIELD(std::uintptr_t,0x790,~std::uintptr_t{},0,"ground_child_render_proxy");
            for(unsigned side=0;side<2;++side) {
                const auto expected=child && child->physics_ready && !child->physics_retired?child->private_bodies[side].actor:0;
                GROUND_CHILD_FIELD(std::uintptr_t,0x520+side*8,~std::uintptr_t{},expected,side?"ground_child_async_body":"ground_child_sync_body");
                if(expected) {
                    const auto read=[](auto p,auto& v){v=Native::Read<std::remove_reference_t<decltype(v)>>(p);return true;};
                    const auto scene=[](std::uintptr_t p){const auto table=Native::Read<std::uintptr_t>(p);
                        return reinterpret_cast<std::uintptr_t(*)(std::uintptr_t)>(Native::Read<std::uintptr_t>(table,0x30))(p);};
                    const auto resolve=[&](std::int16_t id){return reinterpret_cast<std::uintptr_t(*)(std::int16_t)>(base+0x2046020)(id);};
                    if(!child->private_bodies[side].Validate(read,scene)
                        || !child->private_scene.Validate(read,resolve)
                        || !child->private_scene.Matches(child->private_bodies[side]))return false;
                }
            }
            GROUND_CHILD_FIELD(std::uintptr_t,0x640,~std::uintptr_t{},0,"ground_child_2d_body");
            GROUND_CHILD_FIELD(unsigned,0x3f8,3,0,"ground_child_streaming_membership");
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){if(predicate)*predicate="ground_child_read_fault";return false;}
#undef GROUND_CHILD_FIELD
    }
    static bool PoseMatches(std::uintptr_t object,const std::array<std::byte,0x30>& captured,
        Owner* failure=nullptr) noexcept {
        __try {
            // All consumed quaternion/translation/scale lanes are exact. The
            // unused translation/scale W padding is not simulation state.
            // Do not accept the native movement epsilon as restore accuracy.
            for(unsigned lane:{0u,1u,2u,3u,4u,5u,6u,8u,9u,10u}) {
                unsigned expected{};std::memcpy(&expected,captured.data()+lane*4,4);
                const auto actual=Native::Read<unsigned>(object,0x270+lane*4);
                if(actual!=expected || (expected&0x7f800000u)==0x7f800000u) {
                    if(failure){failure->pose_component=object;failure->pose_offset=0x270+lane*4;
                        failure->pose_expected=expected;failure->pose_actual=actual;}
                    return false;
                }
            }
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool Free(std::uintptr_t base,std::uintptr_t allocation) noexcept {
        __try {if(allocation)reinterpret_cast<void(*)(void*)>(base+0xd46a00)(reinterpret_cast<void*>(allocation));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool ResponsesPrivate(std::uintptr_t object,std::uintptr_t defaults) noexcept {
        __try {
            const auto current=Native::Read<Header>(object,0x490),source=Native::Read<Header>(defaults,0x490);
            for(const auto& h:{current,source})
                if(h.count<0 || h.capacity<h.count || h.capacity>64 || (h.capacity?!h.data:bool(h.data))
                    || h.data>UINTPTR_MAX-std::size_t(h.capacity)*16)return false;
            return !current.capacity || !source.capacity || current.data>=source.data+std::size_t(source.capacity)*16
                || source.data>=current.data+std::size_t(current.capacity)*16;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool PoseAuxMatches(std::uintptr_t object,const std::array<std::byte,120>& captured,
        unsigned flags,Owner* failure=nullptr) noexcept {
        __try {
            const auto actual_flags=Native::Read<unsigned>(object,0x240)&0xfu;
            if(actual_flags!=(flags&0xfu)) {
                if(failure){failure->pose_component=object;failure->pose_offset=0x240;
                    failure->pose_expected=flags&0xfu;failure->pose_actual=actual_flags;}return false;
            }
            unsigned cursor{};
            for(const auto range:{std::pair{0x24cu,28u}, {0x2a0u,28u}, {0x2c0u,24u}, {0x2e0u,28u}, {0x300u,12u}}) {
                for(unsigned offset=0;offset<range.second;offset+=4,cursor+=4) {
                    unsigned expected{};std::memcpy(&expected,captured.data()+cursor,4);
                    const auto actual=Native::Read<unsigned>(object,range.first+offset);
                    if(actual!=expected) {
                        if(failure){failure->pose_component=object;failure->pose_offset=range.first+offset;
                            failure->pose_expected=expected;failure->pose_actual=actual;}return false;
                    }
                }
            }
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool Private(const Owner& owner,bool require_pose=true) noexcept {
        __try {
            if(!owner.graph_ready || !owner.child_count || owner.child_count>16 || owner.material_count>256 || !owner.graph_entries)return false;
            const auto b=owner.base,c=owner.object;
            if(require_pose && (!PoseMatches(c,owner.captured_transform)
                || !PoseAuxMatches(c,owner.captured_transform_auxiliary,owner.captured_transform_flags)))return false;
            if(Native::Read<std::uintptr_t>(c)!=b+0x33566f8 || Native::Read<std::uintptr_t>(c,0x20)!=owner.outer
                || Native::Read<std::uintptr_t>(c,0x190)!=owner.outer
                || (Native::Read<unsigned>(c,0x188)&(1u|2u|4u|0x40000u|0x200000u|0x1c000000u|0x20000000u))
                || (Native::Read<unsigned char>(c,0x11c)&0x40) || (Native::Read<unsigned char>(c,0x7bc)&0x40)
                || Native::Read<std::uintptr_t>(c,0x1d0) || Native::Read<int>(c,0x1e0)
                || Native::Read<std::uintptr_t>(c,0x820)!=owner.graph_entries
                || Native::Read<int>(c,0x828)!=int(owner.child_count)
                || Native::Read<int>(c,0x82c)!=int(owner.child_count))return false;
            const auto controller=Native::Read<std::uintptr_t>(c,0x8b0);
            if(!controller || Native::Read<int>(c,0x8c0)!=3 || Native::Read<std::uintptr_t>(controller)!=b+0x3510a68
                || Native::Read<std::uintptr_t>(controller,8)!=c+0x820
                || Native::Read<std::uintptr_t>(controller,16)!=owner.graph_callback
                || !Native::Read<std::uint64_t>(controller,0x20))return false;
            for(unsigned i=0;i<owner.child_count;++i) {
                const auto& child=owner.children[i];
                if(require_pose && (!PoseMatches(child.object,child.captured_transform)
                    || !PoseAuxMatches(child.object,child.captured_transform_auxiliary,child.captured_transform_flags)))return false;
                if(Native::Read<std::uintptr_t>(owner.graph_entries,i*0x28)!=child.object
                    || !ChildPrivate(b,child.object,c,owner.outer,nullptr,nullptr,&child) || Native::Read<std::uintptr_t>(child.object,0x920)!=child.asset)return false;
                const auto responses=Native::Read<Header>(child.object,0x490);
                if(responses.data!=child.body_responses || responses.count!=int(child.body_response_count)
                    || responses.capacity!=responses.count || responses.count<0 || responses.count>32
                    || (responses.count&&!responses.data))return false;
                if(child.displaced_body_responses && child.displaced_body_responses==responses.data)return false;
                for(unsigned side=0;side<2;++side) {
                    const auto header=Native::Read<Header>(owner.graph_entries+i*0x28,8+side*16);
                    if(header.data!=child.material_arrays[side] || header.count!=int(child.material_counts[side])
                        || header.count<0 || header.count>32 || header.capacity!=header.count || (header.count&&!header.data))return false;
                    for(int j=0;j<header.count;++j)
                        if(Native::Read<std::uintptr_t>(header.data,j*8)!=child.material_objects[side][j])return false;
                }
            }
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
};
}
Status Sc6ReplayGroundDebrisState::ConstructColdGraph(std::size_t ordinal,std::size_t budget,ColdRoot& output) const noexcept {
    using Native=Sc6ReplayColdParticleOwner;
    const auto fail=[&](FailureCode code,const char* check){output.check=check;return Status::failure(code);};
    if(!state_ || ordinal>=state_->count || output.phase!=ColdRoot::Phase::Cold || !output.detached || output.graph_entered
        || output.graph_lease.registered() || !GroundColdNative::Cold(output))return fail(FailureCode::IllegalTransition,"ground_graph_domain");
    if(state_->thread!=GetCurrentThreadId())return fail(FailureCode::WrongThread,"ground_graph_thread");
    const auto& root=state_->roots[ordinal];
    if(output.source!=root.object || !output.lease.Validate().ok() || !state_->configuration_lease
        || !state_->configuration_lease->Validate().ok() || !GroundGraphNative::Identity(output.base)
        || !Native::MaterialNativeSignatures(output.base))return fail(FailureCode::GenerationMismatch,"ground_graph_dependencies");
    // Native phase 2 contains both pending and completed impulse callbacks.
    if(root.mode!=2 || (root.callable[2]!=state_->base+0x89ff80 && root.callable[2]!=state_->base+0x8a0230))
        return fail(FailureCode::UnsupportedContent,"ground_graph_callback_phase");
    constexpr std::size_t native_allowance=16*1024*1024;
    if(output.owned_bytes()>budget || native_allowance+32768>budget-output.owned_bytes())
        return fail(FailureCode::CapacityExceeded,"ground_graph_budget");
    try {
        ReplayGroundDebrisConfiguration::NativeView configuration;
        if(!root.configuration_image.BuildNativeView(configuration) || !Native::PassiveOuterModify(state_->base,output.object))
            return fail(FailureCode::UnsupportedContent,"ground_graph_configuration");
        auto* lock=reinterpret_cast<LPCRITICAL_SECTION>(state_->base+0x429f678);
        if(!TryEnterCriticalSection(lock))return fail(FailureCode::ContextUnavailable,"ground_graph_gc_busy");
        struct Unlock {LPCRITICAL_SECTION lock;~Unlock(){LeaveCriticalSection(lock);}} unlock{lock};
        if(Native::Read<int>(state_->base,0x429f674) || Native::Read<int>(state_->base,0x429fa54)
            || Native::Read<unsigned char>(state_->base,0x429f648) || Native::Read<unsigned char>(state_->base,0x429fa0c))
            return fail(FailureCode::ContextUnavailable,"ground_graph_gc_pending");
        output.graph_entered=true;output.phase=ColdRoot::Phase::Entered;output.allowance+=native_allowance;
        output.captured_transform=root.transform;
        output.captured_transform_auxiliary=root.transform_auxiliary;output.captured_transform_flags=root.transform_flags;
        output.check="ground_graph_acquisition_hold_required";
        if(!GroundGraphNative::Configure(output.base,output.object,configuration.configuration.data()))
            return fail(FailureCode::RestoreWriteFailed,"ground_graph_configuration_hold_required");
        output.graph_entries=GroundGraphNative::Allocate(output.base,std::size_t(root.ring.count)*0x28);
        if(!output.graph_entries || !State::write(output.object+0x820,State::Array{output.graph_entries,root.ring.count,root.ring.count}))
            return fail(FailureCode::RestoreWriteFailed,"ground_graph_ring_hold_required");
        std::array<void*,272> references{};unsigned reference_count{};
        for(const auto& mesh:state_->meshes)if(mesh.root==root.object) {
            if(mesh.ordinal!=output.child_count || output.child_count==output.children.size())return fail(FailureCode::GenerationMismatch,"ground_graph_child_order");
            auto* type=reinterpret_cast<RC::Unreal::UClass*>(mesh.class_object);
            auto* defaults=type->GetClassDefaultObject().Get();
            if(!Native::Live(defaults) || defaults->GetClassPrivate()!=type
                || Native::Read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(defaults))!=state_->base+0x36cefb0)
                return fail(FailureCode::IdentityMismatch,"ground_graph_child_defaults");
            Native::Members before{},after{};unsigned count{},current{};
            if(!Native::ReadMembers(root.actor,before,count))return fail(FailureCode::GenerationMismatch,"ground_graph_before_membership");
            auto& child=output.children[output.child_count++];child.source=mesh.component;child.asset=mesh.asset;
            child.captured_transform=mesh.transform;
            child.captured_transform_auxiliary=mesh.transform_auxiliary;child.captured_transform_flags=mesh.visibility;
            child.source_actors=mesh.body_actors;child.initial_body_states=mesh.initial_body_states;
            child.initial_body_velocity=mesh.initial_body_velocity;child.initial_body_velocity_flag=mesh.initial_body_velocity_flag;
            void* created{};
            if(!Native::NativeDuplicate(output.base,defaults,reinterpret_cast<void*>(output.object),created))
                return fail(FailureCode::RestoreWriteFailed,"ground_graph_child_constructor");
            child.object=reinterpret_cast<std::uintptr_t>(created);
            if(!Native::Live(created) || child.object==child.source || created==defaults
                || !State::write(output.graph_entries+mesh.ordinal*0x28,child.object)
                || !State::bind(output.base,child.object,child.weak))
                return fail(FailureCode::GenerationMismatch,"ground_graph_child_identity");
            references[reference_count++]=created;
            // The native ground class has no custom GC-reference callback for
            // its ring. Retain each child before removing its actor membership,
            // including every partial-acquisition failure path.
            const auto acquired=child.acquisition_lease.Acquire(output.base,{&created,1},budget-output.owned_bytes());
            if(!acquired.ok())return fail(acquired.code,"ground_graph_child_acquisition_lease");
            if(!Native::ReadMembers(root.actor,after,current) || current!=count+1
                || std::count(after.begin(),after.begin()+current,child.object)!=1
                || !Native::NativeRemove(output.base,reinterpret_cast<void*>(root.actor),created)
                || !Native::ReadMembers(root.actor,after,current) || count!=current || before!=after)
                return fail(FailureCode::GenerationMismatch,"ground_graph_child_detachment");
            if(!GroundGraphNative::ChildPrivate(output.base,child.object,output.object,root.actor,&output.check,&output.child_observed))
                return Status::failure(FailureCode::GenerationMismatch);
            if(!GroundGraphNative::Mesh(output.base,child.object,mesh.asset)
                || !GroundGraphNative::RemoveStreaming(output.base,child.object)
                || !ReplayGroundPose::InstallCold([](auto p,auto& v){return State::read(p,v);},
                    [](auto p,const auto& v){return State::write(p,v);},child.object,mesh.transform,mesh.transform_auxiliary,mesh.visibility))
                return fail(FailureCode::RestoreWriteFailed,"ground_graph_child_setters");
            if(!State::write(child.object+0x318,mesh.mobility))return fail(FailureCode::RestoreWriteFailed,"ground_graph_mobility");
            if(!GroundGraphNative::ResponsesPrivate(child.object,reinterpret_cast<std::uintptr_t>(defaults)))
                return fail(FailureCode::GenerationMismatch,"ground_graph_default_response_ownership");
            child.body_responses=mesh.body_configuration.response_count()?GroundGraphNative::Allocate(output.base,
                std::size_t(mesh.body_configuration.response_count())*16):0;
            child.body_response_count=mesh.body_configuration.response_count();
            if(!mesh.body_configuration.ApplyCold([](auto p,auto& v){return State::read(p,v);},
                [](auto p,const auto& v){return State::write(p,v);},child.object+0x430,child.body_responses,
                &output.body_configuration_offset,&output.child_observed,&child.displaced_body_responses))
                return fail(FailureCode::RestoreWriteFailed,"ground_graph_body_configuration");
            for(unsigned side=0;side<2;++side) {
                State::Array captured{};std::memcpy(&captured,root.entries.data()+mesh.ordinal*0x28+8+side*16,sizeof(captured));
                const auto storage=captured.count?GroundGraphNative::Allocate(output.base,std::size_t(captured.count)*8):0;
                if((captured.count&&!storage) || !State::write(output.graph_entries+mesh.ordinal*0x28+8+side*16,State::Array{storage,captured.count,captured.count}))
                    return fail(FailureCode::RestoreWriteFailed,"ground_graph_material_array");
                child.material_arrays[side]=storage;child.material_counts[side]=unsigned(captured.count);
                for(int i=0;i<captured.count;++i) {
                    const State::Material* image{};
                    for(const auto& material:state_->materials)if(material.root==root.object && material.mesh_ordinal==mesh.ordinal
                        && material.array==side && material.ordinal==unsigned(i)) {if(image)return fail(FailureCode::GenerationMismatch,"ground_graph_duplicate_material");image=&material;}
                    if(!image || output.material_count==output.materials.size())return fail(FailureCode::GenerationMismatch,"ground_graph_material_inventory");
                    std::uintptr_t fresh{};
                    for(unsigned j=0;j<output.material_count;++j)if(output.materials[j].source==image->object)fresh=output.materials[j].object;
                    if(!fresh) {
                        fresh=GroundGraphNative::Material(output.base,output.object,image->values.parent);
                        if(!Native::Live(reinterpret_cast<void*>(fresh)) || fresh==image->object)return fail(FailureCode::GenerationMismatch,"ground_graph_material_constructor");
                        output.materials[output.material_count++]={image->object,fresh};
                        references[reference_count++]=reinterpret_cast<void*>(fresh);
                        reinterpret_cast<RC::Unreal::UObject*>(fresh)->SetRootSet();
                        Native::MaterialNativeCalls native{output.base,child.object};
                        for(const auto& value:image->values.scalars)if(!native.Scalar(fresh,value.name,value.value))return fail(FailureCode::RestoreWriteFailed,"ground_graph_material_scalar");
                        for(const auto& value:image->values.vectors)if(!native.Vector(fresh,value.name,value.value.data()))return fail(FailureCode::RestoreWriteFailed,"ground_graph_material_vector");
                        ReplayParticleMaterialGraph::Slot observed;
                        if(!ReplayParticleMaterialGraph::CaptureSlot(output.base,output.object,fresh,observed) || !image->values.ValuesEqual(observed))
                            return fail(FailureCode::GenerationMismatch,"ground_graph_material_values");
                    }
                    if(!State::write(storage+std::size_t(i)*8,fresh))return fail(FailureCode::RestoreWriteFailed,"ground_graph_material_binding");
                    child.material_objects[side][i]=fresh;
                }
            }
            for(unsigned i=0;i<mesh.material_count;++i) {
                const auto source=mesh.material_overrides[i];auto replacement=source;bool translated{};
                for(unsigned j=0;j<output.material_count;++j)if(output.materials[j].source==source){replacement=output.materials[j].object;translated=true;}
                if(source && !translated && !state_->configuration_lease->ValidateObject(reinterpret_cast<void*>(source)).ok())
                    return fail(FailureCode::GenerationMismatch,"ground_graph_override_dependency");
                if(!GroundGraphNative::Override(output.base,child.object,i,replacement))return fail(FailureCode::RestoreWriteFailed,"ground_graph_override");
            }
            if(!GroundGraphNative::RemoveStreaming(output.base,child.object)
                || !GroundGraphNative::ChildPrivate(output.base,child.object,output.object,root.actor,&output.check,&output.child_observed))
                return Status::failure(FailureCode::GenerationMismatch);
        }
        if(output.child_count!=unsigned(root.ring.count) || !State::write(output.object+0x838,output.object)
            || !State::write(output.object+0x850,root.ring_values) || !State::write(output.object+0x830,root.mode)
            || !State::write(output.object+0xa08,root.clock)
            || !ReplayGroundPose::InstallCold([](auto p,auto& v){return State::read(p,v);},
                [](auto p,const auto& v){return State::write(p,v);},output.object,root.transform,root.transform_auxiliary,root.transform_flags))
            return fail(FailureCode::RestoreWriteFailed,"ground_graph_root_values");
        output.graph_callback=root.callable[2];
        if(!GroundGraphNative::Bind(output.base,output.object,output.graph_callback))
            return fail(FailureCode::RestoreWriteFailed,"ground_graph_controller_binding");
        const auto held=output.graph_lease.Acquire(output.base,{references.data(),reference_count},budget-output.owned_bytes());
        if(!held.ok())return fail(held.code,"ground_graph_lease");
        output.graph_ready=true;
        if(output.owned_bytes()>budget || !GroundGraphNative::Private(output,false))
            return fail(FailureCode::GenerationMismatch,"ground_graph_private_validation");
        // A pose mismatch rejects preparation but leaves the fully owned,
        // structurally private graph eligible for native/GPU retirement.
        output.phase=ColdRoot::Phase::Cold;
        if(!GroundGraphNative::PoseMatches(output.object,output.captured_transform,&output)
            || !GroundGraphNative::PoseAuxMatches(output.object,output.captured_transform_auxiliary,output.captured_transform_flags,&output))
            return fail(FailureCode::GenerationMismatch,"ground_graph_root_pose");
        for(unsigned i=0;i<output.child_count;++i)
            if(!GroundGraphNative::PoseMatches(output.children[i].object,output.children[i].captured_transform,&output)
                || !GroundGraphNative::PoseAuxMatches(output.children[i].object,output.children[i].captured_transform_auxiliary,
                    output.children[i].captured_transform_flags,&output))
                return fail(FailureCode::GenerationMismatch,"ground_graph_child_pose");
        output.check="cold_graph_ready";return Status::success();
    } catch(...) {return fail(FailureCode::CapacityExceeded,"ground_graph_exception_hold_required");}
}
