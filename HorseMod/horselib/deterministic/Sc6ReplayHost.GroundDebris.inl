// Included inside Horse::Deterministic. This retains the original native
// lifecycle in place; none of these ownership witnesses are installed in A/C.
struct Sc6ReplayGroundDebrisState::State {
    struct Array {std::uintptr_t data{};int count{},capacity{};friend bool operator==(const Array&,const Array&)=default;};
    struct Root {
        std::uintptr_t object{},actor{},controller{},class_object{};
        Array ring{},delegate{};
        std::array<std::int32_t,2> weak{};
        std::array<std::uintptr_t,6> callable{};
        std::array<std::byte,0x78> configuration{};
        ReplayGroundDebrisConfiguration configuration_image;
        std::array<std::byte,0x30> transform{};
        std::array<std::byte,120> transform_auxiliary{};
        unsigned transform_flags{};
        // Ring+30..63: copied placement scalar defaults, fade request/duration
        // and elapsed time. 1408988B0 writes +58/+5C/+60; no owning headers.
        std::array<std::byte,0x34> ring_values{};
        unsigned component_flags{};
        std::array<unsigned,3> clock{};
        std::array<std::byte,0x28*16> entries{};
        unsigned char auto_destroy{},mode{};
    };
    std::uintptr_t base{},manager{},world{};
    std::uint64_t deactivation_name{};
    std::uint32_t thread{};
    std::array<Root,4> roots{};std::size_t count{};
    std::vector<Mesh> meshes;
    struct Material {
        std::uintptr_t slot{},object{},root{};
        unsigned mesh_ordinal{},array{},ordinal{};
        std::array<std::int32_t,2> weak{};
        ReplayParticleMaterialGraph::Slot values;
    };
    std::vector<Material> materials;
    std::vector<void*> objects;
    std::unique_ptr<Sc6ReplayObjectLease> lease;
    std::vector<void*> configuration_assets;
    std::unique_ptr<Sc6ReplayObjectLease> configuration_lease;
    std::array<std::unique_ptr<ReplayPhysicsRemovalOperation>,2> physics;
    std::array<std::uintptr_t,2> scenes{};
    ReplayPhysicsPublicationLists publication;
    enum class Physics { Unprepared, Prepared, Removing, Detached, Recovering, Recovered, Poisoned };
    Physics physics_phase{Physics::Unprepared};
    enum class Retirement {Retained,Ready,Retiring,Retired,Poisoned};
    Retirement retirement{Retirement::Retained};
    unsigned retired_roots{};
    static constexpr std::size_t retirement_scratch_bytes=4096;
    const char* physics_check{"none"};
    struct BodyConsumerDiagnostic {
        std::uintptr_t actor{},component{},body{},shape{},query_handle{},type{};
        unsigned actor_index{},mesh_index{},shape_index{},body_flags{},actor_flags{},shape_flags{},core_flags{},constraints{};
    } body_diagnostic;
    ReplayPhysicsBodyInventory body_inventory{};
    bool unrelated_dynamic{};std::uintptr_t unrelated_dynamic_actor{};
    struct NotificationDiagnostic {
        std::uintptr_t scene{},core{},actor{},simulation{},callback{},callback_type{};
        unsigned count{},slot{},client{},actor_flags{},simulation_flags{};bool owned{};
    } notification_diagnostic;
    void observe_notifications() const noexcept {
        const auto& n=notification_diagnostic;
        if(n.count)RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] ground queued notification scene={:x} slot={} count={} core={:x} actor={:x} simulation={:x} owned={} client={} actor_flags={:x} simulation_flags={:x} callback={:x} callback_type={:x} read_only=true\n"),
            n.scene,n.slot,n.count,n.core,n.actor,n.simulation,n.owned,n.client,n.actor_flags,n.simulation_flags,n.callback,n.callback_type);
    }
    struct BodyProof {std::uintptr_t actor{},body{};};
    std::array<BodyProof,32> body_proofs{};unsigned body_proof_count{};
    bool engine_physics_quiescent() const noexcept {
        __try {
            const auto ptr=[](std::uintptr_t p,unsigned o=0){return *reinterpret_cast<const std::uintptr_t*>(p+o);};
            const auto integer=[](std::uintptr_t p,unsigned o=0){return *reinterpret_cast<const int*>(p+o);};
            const auto owner=ptr(world,0x1c8);
            if(!owner || ptr(owner,0x160))return false;
            // Native1420289D0 removes body references from these deferred
            // engine queues. Admit their completed state, never copy them.
            for(unsigned offset:{0x198u,0x1e0u,0x228u})
                if(integer(owner,offset+0x10) || integer(owner,offset+0x20))return false;
            // Completed transform outputs are separately detached/recovered by
            // publication. They need not be empty after native consumption.
            // Both substep input maps must be logically empty. Native050120
            // addresses both buffers; an inactive-buffer guess is insufficient.
            const auto asynchronous=*reinterpret_cast<const unsigned char*>(owner);
            for(unsigned offset:{0x370u,0x380u}) {
                // Disabled async storage is uninitialized, not a null binding.
                if(offset==0x380 && !asynchronous)continue;
                const auto substep=ptr(owner,offset);
                if(!substep)return false;
                for(unsigned side:{0u,0x50u}) {
                    const auto count=integer(substep+side,8),free=integer(substep+side,0x34);
                    if(count<0 || count>4096 || count!=free)return false;
                }
            }
            // Body-instance retirement also removes keyed actor records. Other
            // live keys may remain; none may retain a quarantined B actor.
            for(unsigned offset:{0x3f0u,0x490u}) {
                const auto map=owner+offset;const auto count=integer(map,8),free=integer(map,0x34);
                const auto bits=integer(map,0x28),capacity=integer(map,0x2c);
                if(count<0 || count>4096 || free<0 || free>count || bits!=count || capacity<bits)return false;
                const auto data=ptr(map),heap=ptr(map,0x20),flags=heap?heap:map+0x10;
                if(count && !data)return false;
                unsigned occupied{};
                for(int i=0;i<count;++i)if((unsigned(integer(flags,(i/32)*4))>>(i%32))&1) {
                    ++occupied;const auto key=ptr(data,i*0x18);
                    for(unsigned b=0;b<body_proof_count;++b)if(key==body_proofs[b].actor)return false;
                }
                if(occupied!=unsigned(count-free))return false;
            }
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
#include "ReplayGroundSceneLock.inl"
    template<class T> static bool read(std::uintptr_t p,T& value) noexcept {
        __try {if(!p)return false;std::memcpy(&value,reinterpret_cast<void*>(p),sizeof(T));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool bind(std::uintptr_t base,std::uintptr_t object,std::array<std::int32_t,2>& weak) noexcept {
        __try {reinterpret_cast<void(*)(void*,const void*)>(base+0xf7bad0)(weak.data(),reinterpret_cast<void*>(object));
            return reinterpret_cast<std::uintptr_t(*)(const void*)>(base+0xf823f0)(weak.data())==object;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool live(std::uintptr_t base,std::uintptr_t object,const std::array<std::int32_t,2>& weak) noexcept {
        __try {return object && reinterpret_cast<std::uintptr_t(*)(const void*)>(base+0xf823f0)(weak.data())==object;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool indexed_live(std::uintptr_t object) noexcept {
        // Update admission observes existing identity; constructing a weak
        // reference can assign a serial and is reserved for owned snapshots.
        __try {
            if(!object)return false;
            auto* value=reinterpret_cast<RC::Unreal::UObject*>(object);
            const auto index=value->GetInternalIndex();
            const auto* item=index>=0?RC::Unreal::FUObjectArray::IndexToObject(index):nullptr;
            return item && item->IsValid(false) && item->GetUObject()==value;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static std::array<int,2> component_identity(std::uintptr_t object) noexcept {
        // FUObjectItem::GetSerialNumber reads its existing field. Do not
        // construct a weak reference here: that can assign a new serial.
        __try {
            if(object) {
                auto* value=reinterpret_cast<RC::Unreal::UObject*>(object);
                const int index=value->GetInternalIndex();
                const auto* item=index>=0?RC::Unreal::FUObjectArray::IndexToObject(index):nullptr;
                if(item && item->IsValid(false) && item->GetUObject()==value) {
                    const int serial=item->GetSerialNumber();
                    if(serial>0)return {index,serial};
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        return {-1,0};
    }
    static bool dormant_tick(std::uintptr_t tick) noexcept {
        unsigned char flags{};std::uintptr_t task{};
        return read(tick+0xc,flags) && !(flags&0x42) && read(tick+0x18,task) && !task;
    }
    template<class T> static bool write(std::uintptr_t p,const T& value) noexcept {
        __try {if(!p)return false;std::memcpy(reinterpret_cast<void*>(p),&value,sizeof(T));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool restore_publication() noexcept {
        const auto release=[&](std::uintptr_t p) noexcept {
            __try {reinterpret_cast<void(*)(void*)>(base+0xd46a00)(reinterpret_cast<void*>(p));return true;}
            __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
        };
        return publication.Recover([](auto p,auto& v){return read(p,v);},[](auto p,const auto& v){return write(p,v);},release);
    }
    bool read_root(std::uintptr_t object,Root& out,bool continuing=false) const noexcept {
        std::uintptr_t type{},actor_type{},owner_root{},check{},target{},task{},callback_owner{},auxiliary{};
        unsigned char flags{};int controller_count{},aux_count{};
        Array delegate{},auxiliary_first{},auxiliary_second{};std::array<std::int32_t,2> delegate_weak{};std::uint64_t name{};
        std::array<unsigned char,11> dispatch{};
        constexpr std::array<unsigned char,11> native_dispatch{0x48,0x8b,0x41,0x10,0x48,0x8b,0x49,8,0x48,0xff,0xe0};
        out.object=object;
        if(!event_consumers() || !physics_step_consumers() || !read(object,type) || type!=base+0x33566f8 || !read(type+0x300,check) || check!=base+0x8a4860
            || !read(type+0x308,check) || check!=base+0x1d5dbe0
            || !read(type+0x360,check) || check!=base+0x898e30
            || !read(object+0x10,out.class_object) || !out.class_object
            || !read(object+0x190,out.actor) || !read(out.actor,actor_type) || actor_type!=base+0x3499188
            || !read(out.actor+0x168,owner_root) || owner_root
            || !read(actor_type+0x410,check) || check!=base+0x1c0fe90
            || !owner_transform_consumers(out.actor)
            || !read(object+0x11c,flags) || !(flags&2)
            || !read(object+0x128,task) || task || !read(object+0x160,target) || target!=object
            || !dormant_tick(object+0x7b0)
            || !read(object+0x808,out.auto_destroy) || out.auto_destroy!=1
            || !read(object+0x830,out.mode) || (out.mode!=2 && !(continuing && out.mode==3))
            || !read(object+0x820,out.ring) || out.ring.count<1 || out.ring.count>16
            || out.ring.capacity<out.ring.count || out.ring.capacity>256 || !out.ring.data
            || !read(object+0x810,delegate) || delegate.count!=1 || delegate.capacity<1 || delegate.capacity>16
            || !read(delegate.data,delegate_weak) || !live(base,manager,delegate_weak)
            || !read(delegate.data+8,name) || name!=deactivation_name
            || !read(object+0x8c0,controller_count) || controller_count!=3
            || !read(object+0x980,aux_count) || aux_count
            || !read(object+0x8d0,auxiliary) || auxiliary
            || !read(object+0x8d8,auxiliary_first) || auxiliary_first.data || auxiliary_first.count || auxiliary_first.capacity
            || !read(object+0x8e8,auxiliary_second) || auxiliary_second.data || auxiliary_second.count || auxiliary_second.capacity
            || !read(object+0x8b0,out.controller) || !out.controller
            || !read(out.controller,out.callable)
            || out.callable[0]!=base+0x3510a68 || out.callable[1]!=object+0x820 || !out.callable[4]
            // Historical capture stays mode2. Continued native mode3 runs the
            // verified fade initialization/update/finish callbacks unchanged.
            || (out.mode==2 ? (out.callable[2]!=base+0x89ff80 && out.callable[2]!=base+0x8a0230)
                : (out.callable[2]!=base+0x89fae0 && out.callable[2]!=base+0x89fe30 && out.callable[2]!=base+0x89fc70))
            || !read(out.callable[0]+0x38,check) || check!=base+0x2d72f0
            || !read(out.callable[0]+0x60,check) || check!=base+0x30f4c60
            || !read(check,dispatch) || dispatch!=native_dispatch
            || !read(object+0x990,out.configuration) || !read(object+0xa08,out.clock)
            || !read(object+0x270,out.transform) || !read(object+0x850,out.ring_values)
            || !read(object+0x240,out.transform_flags)
            || !ReplayGroundPose::Capture([](auto p,auto& v){return read(p,v);},object,out.transform_auxiliary)
            || !read(object+0x188,out.component_flags))return false;
        out.delegate=delegate;
        for(int i=0;i<out.ring.count;++i) {
            std::array<std::byte,0x28> entry{};
            if(!read(out.ring.data+i*0x28,entry))return false;
            std::memcpy(out.entries.data()+i*0x28,entry.data(),entry.size());
        }
        return continuing?indexed_live(object):bind(base,object,out.weak);
    }
    bool event_consumers() const noexcept {
        // 140CF2760 forwards the root to manager virtual+600. The FName alone
        // does not establish its implementation. Downstream manager+388 trace
        // listeners retain their separate composed completion-ownership gate.
        __try {
            std::uintptr_t table{},target{};std::uint64_t name{};
            if(!read(manager,table) || table!=base+0x3356f68
                || !read(table+0x600,target) || target!=base+0x89f5f0)return false;
            auto* event=reinterpret_cast<RC::Unreal::UObject*>(manager)->GetFunctionByNameInChain(L"OnGroundDebrisDeactivated");
            return event && (static_cast<unsigned>(event->GetFunctionFlags())&0x400)
                && !event->GetScript().Num()
                && reinterpret_cast<std::uintptr_t>(event->GetFuncPtr())==base+0xcf2760
                && read(reinterpret_cast<std::uintptr_t>(event)+0x18,name) && name==deactivation_name;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool read_event_name() noexcept {
        __try {
            std::uintptr_t table{};
            if(!read(manager,table) || table!=base+0x3356f68)return false;
            auto* event=reinterpret_cast<RC::Unreal::UObject*>(manager)->GetFunctionByNameInChain(L"OnGroundDebrisDeactivated");
            return event && read(reinterpret_cast<std::uintptr_t>(event)+0x18,deactivation_name);
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool owner_transform_consumers(std::uintptr_t actor) const noexcept {
        // 142028750 calls owner+410 even when movement is below tolerance.
        // 141C0FE90 uses the zero vector for this admitted null owner root,
        // but may still dispatch kill-height behavior. Inspect its predicate;
        // never invoke that callback to discover whether it has side effects.
        __try {
            std::uintptr_t type{},target{},root{},settings{},first_stream{},stream_class{};
            Array streaming{};unsigned char bounds{};unsigned zero_bits{},kill_bits{};
            if(!indexed_live(actor) || !read(actor,type) || type!=base+0x3499188
                || !read(actor+0x168,root) || root || !read(type+0x138,target) || target!=base+0x1c204e0
                || !world || reinterpret_cast<std::uintptr_t(*)(const void*)>(target)(reinterpret_cast<void*>(actor))!=world
                || !read(world+0x88,streaming) || streaming.count<0 || streaming.count>4096
                || streaming.capacity<streaming.count || streaming.capacity>4096
                || (streaming.capacity&&!streaming.data))return false;
            // The native streaming override's ancestry query calls1425A1A00.
            // Do not let this observational lookup initialize that class.
            if(streaming.count && (!read(streaming.data,first_stream)
                || (first_stream && (!read(base+0x43c1ec0,stream_class) || !stream_class))))return false;
            settings=reinterpret_cast<std::uintptr_t(*)(const void*,bool,bool)>(base+0x21bc760)(reinterpret_cast<void*>(world),true,true);
            if(!indexed_live(settings) || !read(settings+0x390,bounds))return false;
            if(!(bounds&1))return true;
            if(!read(settings+0x394,kill_bits) || !read(base+0x418ac50,zero_bits)
                || (kill_bits&0x7f800000u)==0x7f800000u || (zero_bits&0x7f800000u)==0x7f800000u)return false;
            float kill{},zero{};std::memcpy(&kill,&kill_bits,4);std::memcpy(&zero,&zero_bits,4);
            return zero>=kill;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool physics_step_consumers(const char** failed=nullptr,std::uintptr_t* failed_field=nullptr) const noexcept {
        std::uintptr_t physics{};
        Horse::Deterministic::ReplayPhysicsStepConsumer::Diagnostic diagnostic;
        using Consumer=Horse::Deterministic::ReplayPhysicsStepConsumer;
        bool valid{};
        if(!world || !read(world+0x1c8,physics) || !physics) {
            diagnostic.check=Consumer::Check::SceneAbsent;diagnostic.field=world?world+0x1c8:0;
        } else valid=Consumer::Admit(base,physics,[](auto p,auto& value){return read(p,value);},diagnostic);
        if(!valid) {
            if(failed)*failed=Consumer::Name(diagnostic.check);
            if(failed_field)*failed_field=diagnostic.field;
        }
        return valid;
    }
    template<class Diagnostic> void observe_physics_callbacks(Diagnostic& diagnostic) const noexcept {
        // Rejection-only evidence. Never call, mutate or admit a delegate.
        if(!world || !read(world+0x1c8,diagnostic.physics_scene) || !diagnostic.physics_scene)return;
        const auto physics=diagnostic.physics_scene;
        unsigned offset{};
        if(diagnostic.owner==physics+0x60 || diagnostic.owner==physics+0x74)offset=0x10;
        else if(diagnostic.owner==physics+0xd0 || diagnostic.owner==physics+0xe4)offset=0x80;
        else return;
        const auto collection=physics+offset;diagnostic.physics_collection=collection;
        if(!read(collection+0x50,diagnostic.physics_count) || !read(collection+0x64,diagnostic.physics_recursion)
            || diagnostic.physics_count<0 || unsigned(diagnostic.physics_count)>diagnostic.physics_callbacks.size())return;
        std::uintptr_t storage{};
        if(!read(collection+0x40,storage))return;
        if(!storage)storage=collection;
        for(int index=diagnostic.physics_count-1;index>=0;--index) {
            auto& row=diagnostic.physics_callbacks[diagnostic.physics_observed++];
            row.entry=storage+std::uintptr_t(index)*0x40;
            if(!read(row.entry+0x30,row.storage_count))return;
            if(!row.storage_count) {row.readable=true;continue;}
            if(!read(row.entry+0x20,row.callable))return;
            if(!row.callable)row.callable=row.entry;
            if(!read(row.callable,row.words))return;
            row.vtable=row.words[0];
            if(!row.vtable || !read(row.vtable+0x68,row.invoke))return;
            row.readable=true;
        }
        diagnostic.physics_complete=true;
    }
    bool navigation_consumers(std::uintptr_t component) const noexcept {
        // Transform propagation141DA79D0 ->141DB1190 ->141C5FF80 can
        // publish bounds to navigation. Bit21 alone reaches141C5EFD0,
        // which returns for no octree. Bit20 can instead reach141C60AC0:
        // even without an octree it reads bounds and traverses child maps.
        __try {
            unsigned flags{};std::uintptr_t actor{},type{},target{},observed_world{},navigation{},octree{};
            unsigned char suppressed{};
            if(!read(component+0x188,flags))return false;
            if(!(flags&0x00300000u))return true;
            if(!read(component+0x190,actor) || !read(actor,type) || type!=base+0x3499188
                || !read(type+0x138,target) || target!=base+0x1c204e0 || !world)return false;
            observed_world=reinterpret_cast<std::uintptr_t(*)(const void*)>(target)(reinterpret_cast<void*>(actor));
            if(observed_world!=world || !read(world+0xe8,navigation))return false;
            if(!navigation)return true;
            if((flags&0x00100000u) && (!read(type+0x5e8,target) || target!=base+0x2d72f0))return false;
            return read(navigation+0x3fa,suppressed)
                && ((suppressed&1) || (!(flags&0x00100000u) && read(navigation+0x238,octree) && !octree));
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool mesh_consumers(std::uintptr_t component,std::uintptr_t root,std::uintptr_t actor) const noexcept {
        // Shared by capture, update and frozen publication admission. Recheck outgoing
        // consumers without allocating another body/material snapshot or using
        // numeric motion as an admission predicate.
        std::uintptr_t type{},outer{},owner{},parent{},route{};int children{},overlaps{},scopes{},delegates{},recursion{};
        // Native141DB1200 calls child+460 for +48 bit40 even with PhysicsOnly
        // and empty overlaps; that route can change the volume and fire callbacks.
        unsigned char collision{},volume_flags{};
        if(!component || !read(component,type) || type!=base+0x36cefb0
            || !read(component+0x20,outer) || outer!=root
            || !read(component+0x190,owner) || owner!=actor
            || !read(component+0x1d0,parent) || parent || !read(component+0x1e0,children) || children
            || !read(component+0x6a8,overlaps) || overlaps || !read(component+0x3d8,scopes) || scopes
            || !read(component+0x3b0,delegates) || delegates || !read(component+0x3c4,recursion) || recursion
            || !read(component+0x65e,collision) || collision!=2
            || !read(component+0x48,volume_flags) || (volume_flags&0x40)
            || !dormant_tick(component+0x110) || !dormant_tick(component+0x7b0)
            || !read(type+0x300,route) || route!=base+0x1d652a0
            || !read(type+0x438,route) || route!=base+0x1da43e0
            || !read(type+0x430,route) || route!=base+0x1db1200
            || !read(type+0x428,route) || route!=base+0x1da6a20
            || !read(type+0x448,route) || route!=base+0x1dbe830
            || !read(type+0x480,route) || route!=base+0x2043340)return false;
        return navigation_consumers(component);
    }
    bool read_mesh(const Root& root,unsigned index,Mesh& out) const noexcept {
        if(!read(root.ring.data+index*0x28,out.component) || !mesh_consumers(out.component,root.object,root.actor)
            || !read(out.component+0x10,out.class_object) || !out.class_object
            || !read(out.component+0x190,out.actor)
            || !read(out.component+0x920,out.asset) || !out.asset
            || !read(out.component+0x420,out.primitive_id)
            || !read(out.component+0x240,out.visibility) || !read(out.component+0x188,out.flags)
            || !read(out.component+0x270,out.transform)
            || !ReplayGroundPose::Capture([](auto p,auto& v){return read(p,v);},out.component,out.transform_auxiliary)
            || !read(out.component+0x318,out.mobility) || out.mobility>2)return false;
        Array overrides{};
        if(!read(out.component+0x808,overrides) || overrides.count<0 || overrides.count>32
            || overrides.capacity<overrides.count || overrides.capacity>64 || (overrides.capacity&&!overrides.data))return false;
        out.material_count=unsigned(overrides.count);
        for(unsigned i=0;i<out.material_count;++i)if(!read(overrides.data+i*8,out.material_overrides[i]))return false;
        if(!out.body_configuration.Capture([](auto p,auto& value){return read(p,value);},out.component+0x430))return false;
        out.root=root.object;out.ordinal=index;
        return bind(base,out.component,out.weak);
    }
    static bool read_initial_physics(Mesh& mesh) noexcept {
        __try {
            const auto body=mesh.component+0x430;
            if(!read(body+0xf0,mesh.body_actors) || !read(body+0x12c,mesh.initial_body_velocity)
                || !read(body+0x74,mesh.initial_body_velocity_flag))return false;
            mesh.initial_body_velocity_flag&=0x40000u;
            if(!mesh.body_actors[0] && !mesh.body_actors[1])return true;
            const auto module=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PhysX3_x64.dll"));
            if(!module || reinterpret_cast<std::uintptr_t>(GetProcAddress(reinterpret_cast<HMODULE>(module),
                "??0PxRigidDynamicGeneratedValues@physx@@QEAA@PEBVPxRigidDynamic@1@@Z"))!=module+0x7810
                || reinterpret_cast<std::uintptr_t>(GetProcAddress(reinterpret_cast<HMODULE>(module),
                "??0PxShapeGeneratedValues@physx@@QEAA@PEBVPxShape@1@@Z"))!=module+0x88a0)return false;
            ReplayGroundShapeState::Binding binding;
            if(!read(mesh.component+0xc,binding.component) || !read(mesh.actor+0xc,binding.owner))return false;
            for(unsigned side=0;side<2;++side)if(const auto actor=mesh.body_actors[side]) {
                std::uintptr_t table{},user{},self{};unsigned tag{};
                if(!read(actor,table) || table!=module+0x19b7c0 || !read(actor+0x10,user) || user!=body+0x200
                    || !read(body+0x200,tag) || tag!=1 || !read(body+0x208,self) || self!=body)return false;
                const auto values=[&](std::uintptr_t p,auto& output) {
                    reinterpret_cast<void*(*)(void*,std::uintptr_t)>(module+0x7810)(output.data(),p);return true;
                };
                // Other phases remain valid retained-owner checkpoints. They
                // do not acquire permission for this initial-state factory.
                mesh.initial_body_states[side].Capture([](auto p,auto& v){return read(p,v);},values,actor,binding);
            }
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool same_root(const Root& a,const Root& b) const noexcept {
        return a.object==b.object && a.actor==b.actor && a.weak==b.weak && a.class_object==b.class_object
            && a.ring==b.ring && a.delegate==b.delegate && a.entries==b.entries
            && a.controller==b.controller && a.callable==b.callable && a.configuration==b.configuration
            && a.clock==b.clock && a.auto_destroy==b.auto_destroy && a.mode==b.mode
            && a.transform==b.transform && a.ring_values==b.ring_values && a.component_flags==b.component_flags;
    }
    bool owned_component(std::uintptr_t actor,std::uintptr_t component) const noexcept {
        Array entries{};std::uintptr_t heap{};int bits{},free{};
        if(!read(actor+0x2c0,entries) || entries.count<0 || entries.count>4096 || entries.capacity<entries.count
            || (entries.count&&!entries.data) || !read(actor+0x2e8,bits) || bits!=entries.count
            || !read(actor+0x2f4,free) || free<0 || free>entries.count || !read(actor+0x2e0,heap)
            || (!heap && bits>128))return false;
        const auto storage=heap?heap:actor+0x2d0;unsigned occupied{},matches{};
        for(int i=0;i<entries.count;++i) {
            unsigned word{};if(!read(storage+(i/32)*4,word))return false;
            if(!(word&(1u<<(i%32))))continue;
            ++occupied;std::uintptr_t member{};if(!read(entries.data+i*16,member))return false;
            matches+=member==component;
        }
        return occupied==unsigned(entries.count-free) && matches==1;
    }
    bool free_pointer(std::uintptr_t p) noexcept {
        __try {reinterpret_cast<void(*)(void*)>(base+0xd46a00)(reinterpret_cast<void*>(p));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool destroy_root(std::uintptr_t object) noexcept {
        __try {reinterpret_cast<void(*)(void*,bool)>(base+0x898e30)(reinterpret_cast<void*>(object),false);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool component_disposal(std::uintptr_t component,bool mesh) noexcept {
        __try {
            const auto ptr=[](std::uintptr_t p,unsigned o=0){return *reinterpret_cast<const std::uintptr_t*>(p+o);};
            const auto count=[](std::uintptr_t p,unsigned o=0){return *reinterpret_cast<const int*>(p+o);};
            const auto type=ptr(component);const auto flags=unsigned(count(component,0x188));
            physics_check="commit_component_type";
            if(type!=base+(mesh?0x36cefb0:0x33566f8))return false;
            physics_check="commit_component_destruction";
            if(flags&0x10000000u)return false;
            if(flags&0x00300000u) {
                // Native141DA66A0 ->141C4F7D0 resolves the owner's world,
                // then uses the guarded141C5EFD0 removal below when a system
                // exists. Keep native flags unchanged; active removal rejects.
                physics_check="commit_navigation_world";
                const auto actor=ptr(component,0x190);
                if(!actor || ptr(actor)!=base+0x3499188 || ptr(ptr(actor),0x138)!=base+0x1c204e0
                    || !world || reinterpret_cast<std::uintptr_t(*)(const void*)>(base+0x1c204e0)(reinterpret_cast<void*>(actor))!=world)return false;
                physics_check="commit_navigation_active_octree";
                const auto navigation=ptr(world,0xe8);
                // 141C5EFD0 returns before lookup, weak bookkeeping and dirty
                // work when no octree exists or native removal is suppressed.
                if(navigation && ptr(navigation,0x238)
                    && !(*reinterpret_cast<const unsigned char*>(navigation+0x3fa)&1))return false;
            }
            // Native141DA5E40 always runs unweld/child cleanup;141FFB640
            // enters body destruction only for F0/F8/210. A created flag can
            // therefore belong to an empty root. Keep it until native teardown.
            physics_check="commit_root_body_ownership";
            if(!mesh && (ptr(component+0x430,0xf0) || ptr(component+0x430,0xf8)
                || ptr(component+0x430,0x210)))return false;
            physics_check="commit_component_task";
            if(ptr(component,0x128))return false;
            physics_check="commit_component_attachment";
            if(ptr(component,0x1d0) || count(component,0x1e0))return false;
            physics_check="commit_component_scopes";
            if(count(component,0x3d8) || count(component,0x3c4))return false;
            physics_check="commit_component_virtuals";
            if(ptr(type,0x360)!=base+(mesh?0x1d99730:0x898e30)
                || ptr(type,0x370)!=base+0x1da5910 || ptr(type,0x2d8)!=base+0x1da9000
                || ptr(type,0x2f0)!=base+0x1d42de0 || ptr(type,0x2f8)!=base+0x1d666f0
                || ptr(type,0x288)!=base+(mesh?0x1dd1180:0x1da66a0)
                || ptr(type,0x2b0)!=base+0x1d99c50 || ptr(type,0x2c0)!=base+(mesh?0x1dd0fb0:0x1da5e40))return false;
            physics_check="commit_component_owner_membership";
            if(!owned_component(ptr(component,0x190),component))return false;
            // Same native EndPlay admission used by particle retirement:
            // non-native empty script returns before dispatch (140F71540).
            physics_check="commit_component_EndPlay";
            if(flags&0x08000000u) {
                const auto name=*reinterpret_cast<const std::uint64_t*>(base+0x43b5538);
                const auto function=reinterpret_cast<std::uintptr_t(*)(const void*,std::uint64_t)>(base+0xf6e0e0)(reinterpret_cast<void*>(component),name);
                if(!function || (count(function,0x88)&0x400) || count(function,0x50))return false;
            }
            {
                physics_check="commit_component_physics_dependencies";
                if((*reinterpret_cast<const unsigned char*>(component+0x3f8)&2) || count(component,0x6a8)
                    || ptr(component+0x430,0xb8) || ptr(component+0x430,0xb0)
                    || ptr(component+0x430,0x100) || ptr(component+0x430,0x210)
                    || ptr(type,0x630)!=base+0x20431d0
                    || ptr(type,0x670)!=base+0x2057160 || ptr(type,0x678)!=base+0x20570e0)return false;
                // Native141D43340 broadcasts these before physics destruction.
                // Unknown listeners or active dispatch cannot be retired here.
                physics_check="commit_component_physics_callbacks";
                if((flags&4) && (count(base+0x40939e0,0x50) || count(base+0x40939e0,0x64)
                    || count(component+0xa0,0x50) || count(component+0xa0,0x64)))return false;
            }
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool disposal_domain() noexcept {
        physics_check="commit_native_identity";
        struct Signature {unsigned offset;std::array<unsigned char,16> bytes;};
        constexpr Signature signatures[]{
            {0x898e30,{0x48,0x89,0x5c,0x24,8,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0x0f,0xb6,0xda}},
            {0x1d99730,{0x48,0x8b,0xc4,0x88,0x50,0x10,0x55,0x56,0x41,0x55,0x48,0x8b,0xec,0x48,0x81,0xec}},
            {0x1d41970,{0x40,0x53,0x48,0x83,0xec,0x20,0x8b,0x81,0x88,1,0,0,0x48,0x8b,0xd9,0x0f}},
            {0x1d43340,{0x40,0x53,0x48,0x83,0xec,0x20,0xf6,0x81,0x88,1,0,0,4,0x48,0x8b,0xd9}},
            {0x1dd0fb0,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0x82,0x4e,0xfd,0xff,0x48,0x8b}},
            {0x20289d0,{0x40,0x53,0x55,0x56,0x48,0x83,0xec,0x20,0x48,0x8b,0xe9,0x48,0x89,0x7c,0x24,0x40}}};
        for(const auto& signature:signatures) {
            std::array<unsigned char,16> actual{};
            if(!read(base+signature.offset,actual) || actual!=signature.bytes)return false;
        }
        physics_check="commit_C_membership";Array slots{};
        if(!read(manager+0x3f8,slots) || slots.count<0 || slots.count>4096 || slots.capacity<slots.count || (slots.count&&!slots.data))return false;
        for(int i=0;i<slots.count;++i) {
            std::uintptr_t member{};if(!read(slots.data+i*0xc0,member))return false;
            for(std::size_t j=0;j<count;++j)if(member==roots[j].object)return false;
        }
        std::array<std::pair<std::uintptr_t,std::size_t>,136> allocations{};unsigned allocation_count{};
        const auto allocation=[&](std::uintptr_t address,std::size_t bytes) {
            if(!bytes)return !address;
            if(!address || address>UINTPTR_MAX-bytes || allocation_count==allocations.size())return false;
            for(unsigned i=0;i<allocation_count;++i)if(address<allocations[i].first+allocations[i].second
                && allocations[i].first<address+bytes)return false;
            allocations[allocation_count++]={address,bytes};return true;
        };
        for(std::size_t j=0;j<count;++j) {
            const auto& root=roots[j];physics_check="commit_root_lifecycle";
            if(!component_disposal(root.object,false))return false;
            physics_check="commit_root_auxiliary";
            // 898E30 also cleans the auxiliary branch. This bounded ring case
            // has no auxiliary mesh/materials/controller to dispose.
            std::uintptr_t auxiliary{};Array first{},second{};
            if(!read(root.object+0x8d0,auxiliary) || auxiliary || !read(root.object+0x8d8,first)
                || !read(root.object+0x8e8,second) || first.data || first.count || first.capacity
                || second.data || second.count || second.capacity)return false;
            physics_check="commit_controller_destructor";
            std::uintptr_t controller_destroy{};
            if(!read(base+0x3510a68+0x48,controller_destroy) || controller_destroy!=base+0x8955b0)return false;
            physics_check="commit_allocation_ownership";
            if(!allocation(root.ring.data,std::size_t(root.ring.capacity)*0x28) || !allocation(root.controller,48))return false;
            for(int i=0;i<root.ring.count;++i)for(unsigned offset:{8u,0x18u}) {
                Array array{};
                if(!read(root.ring.data+i*0x28+offset,array) || array.count<0 || array.capacity<array.count || array.capacity>64
                    || !allocation(array.data,std::size_t(array.capacity)*8))return false;
            }
            physics_check="commit_material_ownership";
            for(const auto& material:materials) {
                bool owned=false;
                for(int i=0;i<root.ring.count;++i)for(unsigned offset:{8u,0x18u}) {
                    Array array{};if(!read(root.ring.data+i*0x28+offset,array))return false;
                    owned|=material.slot>=array.data && material.slot<array.data+std::size_t(array.count)*8;
                }
                if(!owned)continue;
                std::uintptr_t type{},outer{};
                if(!read(material.object,type) || type!=base+0x391ee70 || !read(material.object+0x20,outer) || outer!=root.object)return false;
            }
        }
        physics_check="commit_mesh_lifecycle";
        for(const auto& mesh:meshes)if(!component_disposal(mesh.component,true))return false;
        physics_check="commit_physics_ownership";
        for(unsigned i=0;i<body_proof_count;++i) {
            std::uintptr_t sim{},first{},second{};
            if(!read(body_proofs[i].actor+0x80,sim) || sim || !read(body_proofs[i].body+0xf0,first)
                || !read(body_proofs[i].body+0xf8,second)
                || (first!=body_proofs[i].actor && second!=body_proofs[i].actor))return false;
        }
        if(!engine_physics_quiescent())return false;
        if(!publication.CanCommit([](auto p,auto& value){return read(p,value);})) {physics_check=publication.failed_check();return false;}
        physics_check="none";return true;
    }
#include "ReplayGroundCollisionDomain.inl"
#include "ReplayGroundCollisionUpdate.inl"
    bool collision_update(std::span<const std::uintptr_t> children,const char*& check,std::uintptr_t& rejected_owner) noexcept {
        const auto module=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PhysX3_x64.dll"));
        if(!module || reinterpret_cast<std::uintptr_t>(GetProcAddress(reinterpret_cast<HMODULE>(module),
            "??0PxRigidDynamicGeneratedValues@physx@@QEAA@PEBVPxRigidDynamic@1@@Z"))!=module+0x7810) {
            check="update_collision_module";rejected_owner=0;return false;
        }
        const auto lookup=[&](short id,std::uintptr_t& result) noexcept {
            __try {result=reinterpret_cast<std::uintptr_t(*)(short)>(base+0x2046020)(id);return result!=0;}
            __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
        };
        return collision_update_domain(module,children,check,rejected_owner,lookup,
            [&](std::uintptr_t weak){return reinterpret_cast<std::uintptr_t(*)(const void*)>(base+0xf823f0)(reinterpret_cast<void*>(weak));});
    }
};
Sc6ReplayGroundDebrisState::Sc6ReplayGroundDebrisState()=default;
namespace {
struct GroundColdNative {
    static bool Identity(std::uintptr_t base) noexcept {
        struct Signature {unsigned offset;std::array<unsigned char,16> bytes;};
        constexpr Signature signatures[]{
            {0xf8d310,{0x4c,0x89,0x44,0x24,0x18,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48}},
            {0x1c27a20,{0x48,0x89,0x5c,0x24,8,0x48,0x89,0x54,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48}},
            {0x898e30,{0x48,0x89,0x5c,0x24,8,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0x0f,0xb6,0xda}}};
        __try {
            for(const auto& entry:signatures)
                if(std::memcmp(reinterpret_cast<void*>(base+entry.offset),entry.bytes.data(),entry.bytes.size()))return false;
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool Cold(Sc6ReplayGroundDebrisState::ColdRoot& owner) noexcept {
        using Native=Sc6ReplayColdParticleOwner;
        __try {
            const auto c=owner.object,b=owner.base;
            owner.flags=Native::Read<unsigned>(c,0x188);
            owner.primary_tick=Native::Read<unsigned char>(c,0x11c);
            owner.secondary_tick=Native::Read<unsigned char>(c,0x7bc);
            if(Native::Read<std::uintptr_t>(c)!=b+0x33566f8 || Native::Read<std::uintptr_t>(c,0x20)!=owner.outer
                || Native::Read<std::uintptr_t>(c,0x190)!=owner.outer
                || (owner.flags&(1u|2u|4u|0x40000u|0x200000u|0x1c000000u|0x20000000u))
                || (owner.primary_tick&0x40) || (owner.secondary_tick&0x40)
                || Native::Read<std::uintptr_t>(c,0x1c8) || Native::Read<std::uintptr_t>(c,0x1d0)
                || Native::Read<int>(c,0x1e0) || Native::Read<std::uintptr_t>(c,0x8b0)
                || Native::Read<int>(c,0x8c0) || Native::Read<std::uintptr_t>(c,0x8d0)
                || Native::Read<std::uintptr_t>(c,0x970) || Native::Read<int>(c,0x980))return false;
            for(unsigned offset:{0x810u,0x820u,0x840u,0x990u,0x9e0u,0x9f0u})
                if(Native::Read<std::uintptr_t>(c,offset) || Native::Read<int>(c,offset+8) || Native::Read<int>(c,offset+12))return false;
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
};
}
Sc6ReplayGroundDebrisState::ColdRoot::~ColdRoot() {
    if(phase!=Phase::Empty && phase!=Phase::Released)__fastfail(FAST_FAIL_INVALID_ARG);
}
std::size_t Sc6ReplayGroundDebrisState::root_count() const noexcept {return state_?state_->count:0;}
Status Sc6ReplayGroundDebrisState::ValidateCapturedOwners() const noexcept {
    if(!state_)return Status::failure(FailureCode::IllegalTransition);
    return state_->count?(state_->lease?state_->lease->Validate():Status::failure(FailureCode::GenerationMismatch)):Status::success();
}
Status Sc6ReplayGroundDebrisState::ConstructColdRoot(std::size_t ordinal,std::size_t budget,ColdRoot& output) const noexcept {
    using Native=Sc6ReplayColdParticleOwner;
    const auto fail=[&](FailureCode code,const char* check){output.check=check;return Status::failure(code);};
    if(!state_ || ordinal>=state_->count || output.phase!=ColdRoot::Phase::Empty || output.lease.registered())
        return fail(FailureCode::IllegalTransition,"cold_root_domain");
    if(state_->thread!=GetCurrentThreadId())return fail(FailureCode::WrongThread,"cold_root_thread");
    constexpr std::size_t allowance=1024*1024;
    if(output.owned_bytes()>budget || allowance+2*sizeof(Native::Members)+8192>budget-output.owned_bytes())
        return fail(FailureCode::CapacityExceeded,"cold_root_budget");
    if(!state_->configuration_lease || !state_->configuration_lease->Validate().ok() || !GroundColdNative::Identity(state_->base))
        return fail(FailureCode::GenerationMismatch,"cold_root_dependencies");
    const auto& root=state_->roots[ordinal];
    output.source=root.object;output.outer=root.actor;output.base=state_->base;
    try {
        if(root.class_object!=Native::Read<std::uintptr_t>(state_->base,0x4153510)
            || !Native::PassiveOuterModify(state_->base,root.actor))return fail(FailureCode::UnsupportedContent,"cold_root_class_outer");
        auto* lock=reinterpret_cast<LPCRITICAL_SECTION>(state_->base+0x429f678);
        if(!TryEnterCriticalSection(lock))return fail(FailureCode::ContextUnavailable,"cold_root_gc_busy");
        struct Unlock {LPCRITICAL_SECTION lock;~Unlock(){LeaveCriticalSection(lock);}} unlock{lock};
        if(Native::Read<int>(state_->base,0x429f674) || Native::Read<int>(state_->base,0x429fa54)
            || Native::Read<unsigned char>(state_->base,0x429f648) || Native::Read<unsigned char>(state_->base,0x429fa0c))
            return fail(FailureCode::ContextUnavailable,"cold_root_gc_pending");
        auto* type=reinterpret_cast<RC::Unreal::UClass*>(root.class_object);
        auto* defaults=type->GetClassDefaultObject().Get();
        if(!Native::Live(defaults) || defaults->GetClassPrivate()!=type
            || Native::Read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(defaults))!=state_->base+0x33566f8)
            return fail(FailureCode::IdentityMismatch,"cold_root_defaults");
        Native::Members before{},after{};unsigned count{},current{};
        if(!Native::ReadMembers(root.actor,before,count))return fail(FailureCode::GenerationMismatch,"cold_root_before_membership");
        output.allowance=allowance;output.phase=ColdRoot::Phase::Entered;
        void* object{};
        if(!Native::NativeDuplicate(state_->base,defaults,reinterpret_cast<void*>(root.actor),object))
            return fail(FailureCode::RestoreWriteFailed,"cold_root_constructor_hold_required");
        output.object=reinterpret_cast<std::uintptr_t>(object);
        if(!Native::Live(object) || object==defaults || output.object==root.object || !GroundColdNative::Cold(output))
            return fail(FailureCode::GenerationMismatch,"cold_root_shape_hold_required");
        void* owners[]{object};
        if(!output.lease.Acquire(state_->base,owners,budget-output.owned_bytes()).ok()
            || !State::bind(state_->base,output.object,output.weak))
            return fail(FailureCode::GenerationMismatch,"cold_root_lease_hold_required");
        if(!Native::ReadMembers(root.actor,after,current) || current!=count+1
            || std::count(after.begin(),after.begin()+current,output.object)!=1
            || !Native::NativeRemove(state_->base,reinterpret_cast<void*>(root.actor),object)
            || !Native::ReadMembers(root.actor,after,current) || current!=count || before!=after)
            return fail(FailureCode::GenerationMismatch,"cold_root_detachment_hold_required");
        output.detached=true;
        if(!GroundColdNative::Cold(output) || output.owned_bytes()>budget)
            return fail(FailureCode::GenerationMismatch,"cold_root_final_hold_required");
        output.phase=ColdRoot::Phase::Cold;output.check="cold_root_ready";return Status::success();
    } catch(...) {return fail(FailureCode::CapacityExceeded,"cold_root_exception_hold_required");}
}
#include "Sc6ReplayGroundColdGraph.inl"
#include "Sc6ReplayGroundColdPhysics.inl"
Status Sc6ReplayGroundDebrisState::DestroyColdRoot(ColdRoot& owner,std::uint64_t preceding_completion) noexcept {
    if(owner.phase==ColdRoot::Phase::Empty) {owner.phase=ColdRoot::Phase::Released;return Status::success();}
    if(owner.phase==ColdRoot::Phase::Destroyed || owner.phase==ColdRoot::Phase::Released)return Status::success();
    if(owner.phase!=ColdRoot::Phase::Cold || !owner.detached || !GroundColdNative::Identity(owner.base)
        || !owner.lease.ValidateObject(reinterpret_cast<void*>(owner.object)).ok()
        || (owner.graph_entered ? (!owner.graph_ready || !owner.graph_lease.Validate().ok() || !GroundGraphNative::Private(owner,false)) : !GroundColdNative::Cold(owner)))
        return Status::failure(FailureCode::UndoFailed);
    owner.phase=ColdRoot::Phase::Entered;owner.check="cold_root_destruction_entered";
    if(!GroundPhysicsNative::Retire(owner))return Status::failure(FailureCode::UndoFailed);
    // These detached POD response buffers were replaced while the children
    // were private. They have no render/native-body consumers. Preserve all
    // graph leases and normal native/GPU retirement for the installed buffers.
    for(auto& child:owner.children)if(child.displaced_body_responses) {
        if(!GroundGraphNative::Free(owner.base,child.displaced_body_responses))return Status::failure(FailureCode::UndoFailed);
        child.displaced_body_responses=0;
    }
    // Native 140898A40 owns child destruction, both MID arrays and controller
    // teardown. All graph references remain leased through the fresh fence.
    State native;native.base=owner.base;
    if(!native.destroy_root(owner.object))return Status::failure(FailureCode::UndoFailed);
    unsigned flags{};
    if(!State::read(owner.object+0x188,flags) || !(flags&0x10000000u) || (flags&7))
        return Status::failure(FailureCode::UndoFailed);
    if(owner.graph_ready)for(unsigned i=0;i<owner.child_count;++i) {
        const auto object=owner.children[i].object;std::uintptr_t proxy{},first{},second{};unsigned streaming{};
        if(!State::read(object+0x188,flags) || !(flags&0x10000000u) || (flags&7)
            || !State::read(object+0x790,proxy) || proxy || !State::read(object+0x520,first) || first
            || !State::read(object+0x528,second) || second || !State::read(object+0x3f8,streaming) || (streaming&3))
            return Status::failure(FailureCode::UndoFailed);
    }
    owner.retirement_serial=preceding_completion;owner.phase=ColdRoot::Phase::Destroyed;
    owner.check="cold_root_GPU_retirement_required";return Status::success();
}
Status Sc6ReplayGroundDebrisState::ReleaseColdRoot(ColdRoot& owner,const ReplayGpuCompletion& completion) noexcept {
    if(owner.phase==ColdRoot::Phase::Released)return Status::success();
    if(owner.phase!=ColdRoot::Phase::Destroyed || !completion.retired()
        || completion.result()!=ReplayGpuCompletion::Result::Complete || completion.submitted_serial()<=owner.retirement_serial)
        return Status::failure(FailureCode::IllegalTransition);
    if(owner.graph_ready && !owner.graph_lease_released) {
        const auto released=owner.graph_lease.Release();if(!released.ok())return released;
        owner.graph_lease_released=true;
    }
    for(auto& child:owner.children)if(child.object && !child.acquisition_released) {
        const auto released=child.acquisition_lease.Release();if(!released.ok())return released;
        child.acquisition_released=true;
    }
    const auto released=owner.lease.Release();if(!released.ok())return released;
    owner.allowance=0;owner.phase=ColdRoot::Phase::Released;owner.check="cold_root_retired";return Status::success();
}
Sc6ReplayGroundDebrisState::~Sc6ReplayGroundDebrisState() {
    // Development guard, not lifecycle recovery. Do not release GC leases
    // beneath queued native render destruction, even after physics is released.
    if(state_ && state_->count && (state_->retirement==State::Retirement::Retiring
        || state_->retirement==State::Retirement::Retired || state_->retirement==State::Retirement::Poisoned))
        __fastfail(FAST_FAIL_INVALID_ARG);
}
bool Sc6ReplayGroundDebrisState::empty() const noexcept {return !state_ || !state_->count;}
std::span<const Sc6ReplayGroundDebrisState::Mesh> Sc6ReplayGroundDebrisState::meshes() const noexcept {
    return state_?std::span<const Mesh>(state_->meshes):std::span<const Mesh>{};
}
std::size_t Sc6ReplayGroundDebrisState::owned_bytes() const noexcept {
    if(!state_)return 0;
    auto bytes=sizeof(State)+state_->meshes.capacity()*sizeof(Mesh)+state_->objects.capacity()*sizeof(void*)
        +state_->materials.capacity()*sizeof(State::Material)
        +(state_->lease?state_->lease->owned_bytes():0)
        +state_->configuration_assets.capacity()*sizeof(void*)
        +(state_->configuration_lease?state_->configuration_lease->owned_bytes():0);
    bytes+=State::retirement_scratch_bytes;
    for(std::size_t i=0;i<state_->count;++i)
        bytes+=state_->roots[i].configuration_image.owned_bytes()-sizeof(ReplayGroundDebrisConfiguration);
    for(const auto& material:state_->materials)
        bytes+=material.values.scalars.capacity()*sizeof(ReplayParticleMaterialGraph::Scalar)
            +material.values.vectors.capacity()*sizeof(ReplayParticleMaterialGraph::Vector);
    for(const auto& physics:state_->physics)if(physics)bytes+=physics->owned_bytes();
    bytes+=state_->publication.owned_bytes();
    return bytes;
}
std::size_t Sc6ReplayGroundDebrisState::update_scratch_bytes() noexcept {
    return sizeof(State)+sizeof(State::Root)+64*sizeof(std::uintptr_t)+sizeof(UpdateDiagnostic)
        +sizeof(ReplayPhysicsRemovalGuard)+512*48+sizeof(ReplayPhysicsNotifications)+2304;
}
Status Sc6ReplayGroundDebrisState::ValidateUpdate(std::uintptr_t base,void* battle,void* world,UpdateDiagnostic& diagnostic) noexcept {
    diagnostic={};
    State s;s.base=base;s.world=reinterpret_cast<std::uintptr_t>(world);
    State::Array slots{};std::array<std::uintptr_t,64> children{};
    const auto fail=[&](const char* check) {diagnostic.check=check;return Status::failure(FailureCode::UnsupportedContent);};
    if(!base || !battle || !world || !State::read(reinterpret_cast<std::uintptr_t>(battle)+0x508,s.manager)
        || !s.manager || !State::read(s.manager+0x3f8,slots) || slots.count<0 || slots.count>4096
        || slots.capacity<slots.count || (slots.count&&!slots.data))return fail("update_manager_slots");
    // Check even before the first ground birth. This entry guard does not
    // replace admission at native consumption if consumers change mid-update.
    const char* physics_check{};
    if(!s.physics_step_consumers(&physics_check,&diagnostic.owner)) {
        s.observe_physics_callbacks(diagnostic);return fail(physics_check);
    }
    for(int i=0;i<slots.count;++i) {
        std::uintptr_t object{},type{};
        if(!State::read(slots.data+i*0xc0,object))return fail("update_root_read");
        if(!object)continue;
        diagnostic.owner=object;
        // Every nonempty secondary slot must identify its consumer domain.
        // A changed root vtable cannot disappear from the inventory by filter.
        if(!State::read(object,type) || type!=base+0x33566f8)return fail("update_root_type");
        if(diagnostic.roots==s.roots.size())return fail("update_root_capacity");
        for(unsigned j=0;j<diagnostic.roots;++j)if(s.roots[j].object==object)return fail("update_duplicate_root");
        if(!s.deactivation_name && !s.read_event_name())return fail("update_event_name");
        auto& root=s.roots[diagnostic.roots];
        if(!s.read_root(object,root,true))return fail("update_root_consumers");
        ++diagnostic.roots;
        for(int j=0;j<root.ring.count;++j) {
            std::uintptr_t child{};
            if(!State::read(root.ring.data+j*0x28,child))return fail("update_child_read");
            diagnostic.owner=child;
            if(diagnostic.meshes==children.size())return fail("update_child_capacity");
            for(unsigned k=0;k<diagnostic.meshes;++k)if(children[k]==child)return fail("update_duplicate_child");
            if(!State::indexed_live(child) || !s.mesh_consumers(child,root.object,root.actor))return fail("update_child_consumers");
            const auto identity=State::component_identity(child);
            diagnostic.children[diagnostic.meshes]={child,root.object,root.actor,identity[0],identity[1],
                static_cast<unsigned>(i),static_cast<unsigned>(j)};
            children[diagnostic.meshes++]=child;
        }
    }
    const bool collision_ok=s.collision_update({children.data(),diagnostic.meshes},physics_check,diagnostic.owner);
    diagnostic.body_inventory=s.body_inventory;
    if(!collision_ok) {
        s.observe_notifications();
        return fail(physics_check);
    }
    diagnostic.owner=0;return Status::success();
}
Status Sc6ReplayGroundDebrisState::Prepare(std::uintptr_t base,void* battle,void* world,std::uint64_t deactivation_name,std::size_t budget) noexcept {
    if(state_)return Status::failure(FailureCode::IllegalTransition);
    if(sizeof(State)>budget)return Status::failure(FailureCode::CapacityExceeded);
    try {
        auto state=std::make_unique<State>();auto& s=*state;
        s.base=base;s.world=reinterpret_cast<std::uintptr_t>(world);s.deactivation_name=deactivation_name;s.thread=GetCurrentThreadId();
        State::Array slots{};
        failed_check_="secondary_slots";
        if(!base || !world || !State::read(reinterpret_cast<std::uintptr_t>(battle)+0x508,s.manager)
            || !State::read(s.manager+0x3f8,slots) || slots.count<0 || slots.count>4096 || slots.capacity<slots.count
            || (slots.count && !slots.data))return Status::failure(FailureCode::GenerationMismatch);
        for(int i=0;i<slots.count;++i) {
            std::uintptr_t owner{},type{};
            if(!State::read(slots.data+i*0xc0,owner))return Status::failure(FailureCode::GenerationMismatch);
            if(!owner)continue;
            if(!State::read(owner,type))return Status::failure(FailureCode::GenerationMismatch);
            if(type!=base+0x33566f8)continue; // Other types retain their existing admission path.
            if(!s.deactivation_name && !s.read_event_name())return Status::failure(FailureCode::IdentityMismatch);
            if(s.count==s.roots.size())return Status::failure(FailureCode::CapacityExceeded);
            for(std::size_t j=0;j<s.count;++j)if(s.roots[j].object==owner)return Status::failure(FailureCode::GenerationMismatch);
            auto& root=s.roots[s.count];failed_check_="root_lifecycle";
            unsigned char registration{};
            if(!s.read_root(owner,root) || !State::read(owner+0x11c,registration) || !(registration&0x40)) {
                unsigned flags{},controllers{};unsigned char mode{},tick{};std::uintptr_t actor{},controller{};
                State::read(owner+0x188,flags);State::read(owner+0x830,mode);State::read(owner+0x11c,tick);
                State::read(owner+0x190,actor);State::read(owner+0x8b0,controller);State::read(owner+0x8c0,controllers);
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] ground capture root rejected object={:x} actor={:x} flags={:x} mode={} tick={:x} controller={:x} controllers={} read_only=true\n"),
                    owner,actor,flags,unsigned(mode),unsigned(tick),controller,controllers);
                return Status::failure(FailureCode::UnsupportedContent);
            }
            ++s.count;
        }
        if(!s.count) {state_=std::move(state);failed_check_="none";return Status::success();}
        auto required=sizeof(State)+64*sizeof(Mesh)+512*sizeof(void*)+256*sizeof(State::Material);
        if(required>budget)return Status::failure(FailureCode::CapacityExceeded);
        s.meshes.reserve(64);s.objects.reserve(256);s.materials.reserve(256);s.configuration_assets.reserve(256);
        s.objects.push_back(world);s.objects.push_back(reinterpret_cast<void*>(s.manager));
        const auto retain=[&](std::uintptr_t object) {
            if(!object)return false;
            if(std::find(s.objects.begin(),s.objects.end(),reinterpret_cast<void*>(object))!=s.objects.end())return true;
            if(s.objects.size()==256)return false;
            s.objects.push_back(reinterpret_cast<void*>(object));return true;
        };
        const auto retain_configuration_asset=[&](std::uintptr_t object) {
            if(!retain(object))return false;
            auto* pointer=reinterpret_cast<void*>(object);
            if(std::find(s.configuration_assets.begin(),s.configuration_assets.end(),pointer)!=s.configuration_assets.end())return true;
            if(s.configuration_assets.size()==256)return false;
            s.configuration_assets.push_back(pointer);return true;
        };
        if(!retain_configuration_asset(s.world) || !retain_configuration_asset(s.manager))
            return Status::failure(FailureCode::CapacityExceeded);
        for(std::size_t r=0;r<s.count;++r) {
            auto& root=s.roots[r];
            failed_check_="configuration_owned_arrays";
            if(!root.configuration_image.Capture([](auto p,auto& value){return State::read(p,value);},root.object+0x990,budget-required)) {
                State::Array rows{},first{},second{};std::uintptr_t auxiliary{},asset{};
                State::read(root.object+0x990,rows);State::read(root.object+0x9c8,auxiliary);State::read(root.object+0x9d0,asset);
                State::read(root.object+0x9e0,first);State::read(root.object+0x9f0,second);
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] ground capture configuration rejected root={:x} rows={}/{} auxiliary={:x}/{:x} arrays={}/{}/{}:{} read_only=true\n"),
                    root.object,rows.count,rows.capacity,auxiliary,asset,first.count,first.capacity,second.count,second.capacity);
                return Status::failure(FailureCode::UnsupportedContent);
            }
            required+=root.configuration_image.owned_bytes()-sizeof(ReplayGroundDebrisConfiguration);
            if(required>budget)return Status::failure(FailureCode::CapacityExceeded);
            if(!root.configuration_image.Assets(retain_configuration_asset))return Status::failure(FailureCode::CapacityExceeded);
            if(!retain(root.object) || !retain_configuration_asset(root.actor) || !retain_configuration_asset(root.class_object))
                return Status::failure(FailureCode::CapacityExceeded);
            for(int i=0;i<root.ring.count;++i) {
                Mesh mesh{};failed_check_="mesh_consumers";
                if(!s.read_mesh(root,i,mesh))return Status::failure(FailureCode::UnsupportedContent);
                failed_check_="mesh_initial_physics_capture";
                if(!State::read_initial_physics(mesh))return Status::failure(FailureCode::UnsupportedContent);
                for(const auto& prior:s.meshes)if(prior.component==mesh.component)return Status::failure(FailureCode::GenerationMismatch);
                if(!retain(mesh.component) || !retain_configuration_asset(mesh.asset) || !retain_configuration_asset(mesh.class_object))
                    return Status::failure(FailureCode::CapacityExceeded);
                s.meshes.push_back(mesh);
                for(unsigned offset:{8u,0x18u}) {
                    State::Array materials{};failed_check_="material_ownership";
                    if(!State::read(root.ring.data+i*0x28+offset,materials) || materials.count<0 || materials.count>32
                        || materials.capacity<materials.count || materials.capacity>64 || (materials.count && !materials.data))
                        return Status::failure(FailureCode::UnsupportedContent);
                    for(int m=0;m<materials.count;++m) {
                        std::uintptr_t material{};
                        if(!State::read(materials.data+m*8,material) || !retain(material))return Status::failure(FailureCode::CapacityExceeded);
                        if(s.materials.size()==256)return Status::failure(FailureCode::CapacityExceeded);
                        constexpr auto maximum_parameters=64*(sizeof(ReplayParticleMaterialGraph::Scalar)+sizeof(ReplayParticleMaterialGraph::Vector));
                        if(required>budget || maximum_parameters>budget-required)return Status::failure(FailureCode::CapacityExceeded);
                        State::Material image{materials.data+m*8,material,root.object,unsigned(i),offset==8?0u:1u,unsigned(m)};
                        failed_check_="material_owned_values";
                        if(!ReplayParticleMaterialGraph::CaptureSlot(base,root.object,material,image.values)
                            || !State::bind(base,material,image.weak)
                            || !retain_configuration_asset(image.values.parent))return Status::failure(FailureCode::UnsupportedContent);
                        Sc6ReplayColdParticleOwner::Witness defaults;
                        if(!Sc6ReplayColdParticleOwner::MaterialSlotDefaults(base,image.values,defaults)) {
                            failed_check_=defaults.check;
                            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] ground capture material defaults rejected object={:x} check={} property={:x} read_only=true\n"),
                                material,RC::to_generic_string(defaults.check),reinterpret_cast<std::uintptr_t>(defaults.visited_property));
                            return Status::failure(FailureCode::UnsupportedContent);
                        }
                        required+=image.values.scalars.capacity()*sizeof(ReplayParticleMaterialGraph::Scalar)
                            +image.values.vectors.capacity()*sizeof(ReplayParticleMaterialGraph::Vector);
                        s.materials.push_back(std::move(image));
                    }
                }
            }
        }
        for(const auto& mesh:s.meshes)for(unsigned i=0;i<mesh.material_count;++i) {
            const auto object=mesh.material_overrides[i];if(!object)continue;
            const bool owned=std::any_of(s.materials.begin(),s.materials.end(),[&](const auto& m){return m.root==mesh.root && m.object==object;});
            if(!owned && !retain_configuration_asset(object))return Status::failure(FailureCode::CapacityExceeded);
        }
        for(const auto& mesh:s.meshes)if(const auto material=mesh.body_configuration.physical_material())
            if(!retain_configuration_asset(material))return Status::failure(FailureCode::CapacityExceeded);
        s.lease=std::make_unique<Sc6ReplayObjectLease>();failed_check_="object_lease";
        auto status=s.lease->Acquire(base,s.objects,budget-required);
        if(!status.ok())return status;
        required+=s.lease->owned_bytes();
        if(required>budget)return Status::failure(FailureCode::CapacityExceeded);
        s.configuration_lease=std::make_unique<Sc6ReplayObjectLease>();failed_check_="configuration_asset_lease";
        status=s.configuration_lease->Acquire(base,s.configuration_assets,budget-required);
        if(!status.ok())return status;
        state_=std::move(state);failed_check_="none";return Status::success();
    } catch(...) {return Status::failure(FailureCode::CapacityExceeded);}
}
Status Sc6ReplayGroundDebrisState::ValidateFrozen() const noexcept {
    if(!state_ || state_->thread!=GetCurrentThreadId())return Status::failure(FailureCode::IllegalTransition);
    // Empty checkpoints still depend on the current world's native step and
    // substep consumers. Preparation can span completion waits; revalidate
    // their real owners even when there is no captured root to read below.
    if(!state_->count)return state_->physics_step_consumers()?Status::success()
        :Status::failure(FailureCode::UnsupportedContent);
    if(!state_->lease)return Status::failure(FailureCode::IllegalTransition);
    auto status=state_->lease->Validate();if(!status.ok())return status;
    for(std::size_t i=0;i<state_->count;++i) {
        State::Root observed{};const auto& root=state_->roots[i];
        if(!State::live(state_->base,root.object,root.weak) || !state_->read_root(root.object,observed)
            || !state_->same_root(root,observed))return Status::failure(FailureCode::GenerationMismatch);
    }
    for(const auto& mesh:state_->meshes) {
        const State::Root* root{};
        for(std::size_t i=0;i<state_->count;++i)if(state_->roots[i].object==mesh.root)root=&state_->roots[i];
        std::uintptr_t component{};
        if(!root || mesh.ordinal>=unsigned(root->ring.count)
            || !State::read(root->ring.data+mesh.ordinal*0x28,component) || component!=mesh.component
            || !State::live(state_->base,mesh.component,mesh.weak)
            || !state_->mesh_consumers(mesh.component,root->object,root->actor))
            return Status::failure(FailureCode::GenerationMismatch);
    }
    for(const auto& material:state_->materials) {
        std::uintptr_t current{};
        if(!State::read(material.slot,current) || current!=material.object)return Status::failure(FailureCode::GenerationMismatch);
    }
    return Status::success();
}
Status Sc6ReplayGroundDebrisState::PreparePhysics(std::size_t budget) noexcept {
    auto status=ValidateFrozen();if(!status.ok())return status;
    auto& s=*state_;
    if(s.physics_phase!=State::Physics::Unprepared)return Status::failure(FailureCode::IllegalTransition);
    if(empty()) {s.physics_phase=State::Physics::Prepared;return Status::success();}
    // Fixed native consumer inventory is temporary stack scratch. Reserve it
    // together with both immovable operations before either can be admitted.
    constexpr auto reservation=2*sizeof(ReplayPhysicsRemovalOperation)+512*48+sizeof(ReplayPhysicsNotifications)+256;
    if(owned_bytes()>budget || reservation>budget-owned_bytes())return Status::failure(FailureCode::CapacityExceeded);
    std::uintptr_t owner{},pending{};unsigned count{};unsigned char asynchronous{};
    std::array<short,2> ids{};
    failed_check_="physics_world";
    if(!State::read(s.world+0x1c8,owner) || !owner || !State::read(owner+0x160,pending) || pending
        || !State::read(owner+4,count) || count>3 || !State::read(owner,asynchronous)
        || !State::read(owner+0xf8,ids[0]) || !State::read(owner+0xfc,ids[1]))return Status::failure(FailureCode::GenerationMismatch);
    const auto module=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PhysX3_x64.dll"));
    if(!module || reinterpret_cast<std::uintptr_t>(GetProcAddress(reinterpret_cast<HMODULE>(module),
        "??0PxRigidDynamicGeneratedValues@physx@@QEAA@PEBVPxRigidDynamic@1@@Z"))!=module+0x7810)
        return Status::failure(FailureCode::IdentityMismatch);
    const auto lookup=[](std::uintptr_t base,short id,std::uintptr_t& result) noexcept {
        __try {result=reinterpret_cast<std::uintptr_t(*)(short)>(base+0x2046020)(id);return result!=0;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    };
    try {
        std::array<bool,64> found{};
        for(unsigned i=0;i<2;++i) {
            if(i==0?!count:(!asynchronous || count<=2))continue;
            if(!lookup(s.base,ids[i],s.scenes[i]))return Status::failure(FailureCode::GenerationMismatch);
            State::SceneLock lock{s.scenes[i],module};
            if(!lock.Enter())return Status::failure(FailureCode::IdentityMismatch);
            std::array<std::uintptr_t,16> selected{};unsigned selected_count{};
            if(!s.physics_domain(module,s.scenes[i],found,selected,selected_count,{},
                [&](std::uintptr_t weak){return reinterpret_cast<std::uintptr_t(*)(const void*)>(s.base+0xf823f0)(reinterpret_cast<void*>(weak));})) {
                const auto& d=s.body_diagnostic;
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] ground body admission check={} scene={} actor_index={} actor={:x} type={:x} component={:x} body={:x} mesh_index={} meshes={} body_flags={:x} actor_flags={:x} constraints={} shape_index={} shape={:x} query_handle={:x} shape_flags={:x} core_flags={:x} before_removal=true\n"),
                    RC::to_generic_string(s.physics_check),i,d.actor_index,d.actor,d.type,d.component,d.body,d.mesh_index,s.meshes.size(),
                    d.body_flags,d.actor_flags,d.constraints,d.shape_index,d.shape,d.query_handle,d.shape_flags,d.core_flags);
                failed_check_=s.physics_check;return Status::failure(FailureCode::UnsupportedContent);
            }
            if(!selected_count)continue;
            auto operation=std::make_unique<ReplayPhysicsRemovalOperation>();
            if(!operation->Prepare(module,s.scenes[i],{selected.data(),selected_count})) {
                failed_check_=operation->failed_check();return Status::failure(FailureCode::RestorePreflightFailed);
            }
            s.physics[i]=std::move(operation);
        }
        failed_check_="body_membership";
        for(std::size_t i=0;i<s.meshes.size();++i)if(!found[i])return Status::failure(FailureCode::UnsupportedContent);
        failed_check_="engine_physics_queues";
        if(!s.engine_physics_quiescent())return Status::failure(FailureCode::UnsupportedContent);
        std::array<std::uintptr_t,32> bodies{};
        for(unsigned i=0;i<s.body_proof_count;++i)bodies[i]=s.body_proofs[i].body;
        if(!s.publication.Prepare(owner,{bodies.data(),s.body_proof_count},[](auto p,auto& v){return State::read(p,v);})) {
            failed_check_=s.publication.failed_check();return Status::failure(FailureCode::UnsupportedContent);
        }
        if(owned_bytes()>budget)return Status::failure(FailureCode::CapacityExceeded);
        s.physics_phase=State::Physics::Prepared;failed_check_="none";return Status::success();
    } catch(...) {return Status::failure(FailureCode::CapacityExceeded);}
}
Sc6ReplayGroundDebrisState::Progress Sc6ReplayGroundDebrisState::AdvanceRemoval() noexcept {
    if(!state_)return Progress::Rejected;
    auto& s=*state_;
    if(s.physics_phase==State::Physics::Poisoned)return Progress::Poisoned;
    if(!ValidateFrozen().ok()) {failed_check_="frozen_owner";return Progress::Rejected;}
    if(s.physics_phase==State::Physics::Detached)return Progress::Complete;
    if(s.physics_phase!=State::Physics::Prepared && s.physics_phase!=State::Physics::Removing)return Progress::Rejected;
    if(!empty() && !s.publication.Detach([](auto p,auto& v){return State::read(p,v);},[](auto p,const auto& v){return State::write(p,v);})) {
        failed_check_=s.publication.failed_check();
        if(s.publication.phase()==ReplayPhysicsPublicationLists::Phase::Poisoned) {s.physics_phase=State::Physics::Poisoned;return Progress::Poisoned;}
        return Progress::Rejected;
    }
    s.physics_phase=State::Physics::Removing;
    for(unsigned i=0;i<s.physics.size();++i)if(auto& operation=s.physics[i]) {
        State::SceneLock lock{s.scenes[i],reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PhysX3_x64.dll"))};
        if(!lock.Enter()) {failed_check_="removal_scene_lock";return Progress::Rejected;}
        const auto result=operation->Advance();
        if(result==Progress::Poisoned) {failed_check_=operation->failed_check();s.physics_phase=State::Physics::Poisoned;}
        if(result!=Progress::Complete)return result;
    }
    s.physics_phase=State::Physics::Detached;return Progress::Complete;
}
Sc6ReplayGroundDebrisState::Progress Sc6ReplayGroundDebrisState::RecoverBodies() noexcept {
    if(!state_)return Progress::Complete;
    auto& s=*state_;
    if(s.retirement!=State::Retirement::Retained && s.retirement!=State::Retirement::Ready)return Progress::Rejected;
    if(s.physics_phase==State::Physics::Poisoned)return Progress::Poisoned;
    if(!ValidateFrozen().ok()) {failed_check_="recovery_frozen_owner";return Progress::Rejected;}
    if(s.physics_phase==State::Physics::Recovered)return Progress::Complete;
    // Admission may fail after leases were acquired but before native removal.
    // Such a rejection must release untouched B even when the rejected queue
    // remains nonquiescent. The native operation's phase is the mutation receipt.
    const bool native_mutation=std::any_of(s.physics.begin(),s.physics.end(),[](const auto& operation) {
        if(!operation)return false;
        const auto phase=operation->phase();
        return phase!=ReplayPhysicsRemovalOperation::Phase::Empty
            && phase!=ReplayPhysicsRemovalOperation::Phase::Retained
            && phase!=ReplayPhysicsRemovalOperation::Phase::Released;
    });
    if(native_mutation && !s.engine_physics_quiescent()) {failed_check_="recovery_engine_physics_queues";return Progress::Rejected;}
    if(s.physics_phase==State::Physics::Removing) {
        const auto result=AdvanceRemoval();if(result!=Progress::Complete)return result;
    }
    s.physics_phase=State::Physics::Recovering;
    for(unsigned i=0;i<s.physics.size();++i)if(auto& operation=s.physics[i]) {
        State::SceneLock lock{s.scenes[i],reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PhysX3_x64.dll"))};
        if(!lock.Enter()) {failed_check_="recovery_scene_lock";return Progress::Rejected;}
        const auto result=operation->Recover();
        if(result==Progress::Poisoned) {failed_check_=operation->failed_check();s.physics_phase=State::Physics::Poisoned;}
        if(result!=Progress::Complete)return result;
        if(!operation->ReleaseRecoveredStorage()) {
            failed_check_=operation->failed_check();
            if(operation->phase()==ReplayPhysicsRemovalOperation::Phase::Poisoned) {
                s.physics_phase=State::Physics::Poisoned;return Progress::Poisoned;
            }
            return Progress::Pending;
        }
    }
    if(!s.restore_publication()) {
        failed_check_=s.publication.failed_check();
        if(s.publication.phase()==ReplayPhysicsPublicationLists::Phase::Poisoned) {s.physics_phase=State::Physics::Poisoned;return Progress::Poisoned;}
        return Progress::Rejected;
    }
    s.physics_phase=State::Physics::Recovered;return Progress::Complete;
}
Status Sc6ReplayGroundDebrisState::ReleaseRecoveredOwners() noexcept {
    if(!state_)return Status::success();
    if(state_->physics_phase!=State::Physics::Recovered)return Status::failure(FailureCode::IllegalTransition);
    auto status=state_->lease?state_->lease->Release():Status::success();
    if(status.ok() && state_->configuration_lease)status=state_->configuration_lease->Release();
    if(status.ok())state_.reset();
    return status;
}
Status Sc6ReplayGroundDebrisState::PrepareCommit() noexcept {
    if(empty())return Status::success();
    auto& s=*state_;
    if(s.physics_phase!=State::Physics::Detached || (s.retirement!=State::Retirement::Retained && s.retirement!=State::Retirement::Ready))
        return Status::failure(FailureCode::IllegalTransition);
    auto status=ValidateFrozen();if(!status.ok())return status;
    if(!s.disposal_domain()) {failed_check_=s.physics_check;return Status::failure(FailureCode::UnsupportedContent);}
    s.retirement=State::Retirement::Ready;failed_check_="none";return Status::success();
}
Status Sc6ReplayGroundDebrisState::CommitNativeOwners() noexcept {
    if(empty())return Status::success();
    auto& s=*state_;
    if(s.retirement==State::Retirement::Retired)return Status::success();
    if(s.retirement!=State::Retirement::Ready)return Status::failure(FailureCode::IllegalTransition);
    // Recheck before the first irreversible native call. The host has already
    // settled target tails and made the explicit seek-wide commit decision.
    auto status=PrepareCommit();if(!status.ok())return status;
    s.retirement=State::Retirement::Retiring;
    const auto poison=[&](const char* check) {
        failed_check_=check;s.retirement=State::Retirement::Poisoned;
        return Status::failure(FailureCode::PresentationFailed);
    };
    for(unsigned i=0;i<s.physics.size();++i)if(auto& operation=s.physics[i]) {
        State::SceneLock lock{s.scenes[i],reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PhysX3_x64.dll"))};
        if(!lock.Enter())return poison("commit_scene_lock");
        if(!operation->ReleaseCommittedStorage())return poison(operation->failed_check());
    }
    const auto release=[&](std::uintptr_t p) noexcept {return s.free_pointer(p);};
    if(!s.publication.Commit([](auto p,auto& value){return State::read(p,value);},release))return poison(s.publication.failed_check());
    for(;s.retired_roots<s.count;++s.retired_roots) {
        const auto& root=s.roots[s.retired_roots];
        if(!s.destroy_root(root.object))return poison("commit_native_root_destroy");
        unsigned flags{};int count{},controller_count{};
        if(!State::read(root.object+0x188,flags) || !(flags&0x10000000u) || (flags&7)
            || !State::read(root.object+0x828,count) || count || !State::read(root.object+0x8c0,controller_count) || controller_count)
            return poison("commit_root_completion");
        for(const auto& mesh:s.meshes)if(mesh.root==root.object) {
            std::uintptr_t proxy{},first{},second{};
            if(!State::read(mesh.component+0x188,flags) || !(flags&0x10000000u) || (flags&7)
                || !State::read(mesh.component+0x790,proxy) || proxy
                || !State::read(mesh.component+0x430+0xf0,first) || first
                || !State::read(mesh.component+0x430+0xf8,second) || second)return poison("commit_mesh_completion");
        }
    }
    s.retirement=State::Retirement::Retired;failed_check_="none";
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] ground native commit roots={} meshes={} controller_and_material_cleanup=native leases_retained=true GPU_drain_required=true\n"),s.count,s.meshes.size());
    return Status::success();
}
Status Sc6ReplayGroundDebrisState::ReleaseCommittedOwners() noexcept {
    if(!state_)return Status::success();
    if(empty()) {state_.reset();return Status::success();}
    if(state_->thread!=GetCurrentThreadId() || state_->retirement!=State::Retirement::Retired)
        return Status::failure(FailureCode::IllegalTransition);
    auto status=state_->lease?state_->lease->Release():Status::success();
    if(status.ok() && state_->configuration_lease)status=state_->configuration_lease->Release();
    if(status.ok())state_.reset();return status;
}
Status Sc6ReplayGroundDebrisState::VisitRoots(void* context,bool(*visitor)(void*,std::uintptr_t)) const noexcept {
    const auto status=ValidateFrozen();if(!status.ok())return status;
    if(!visitor)return Status::failure(FailureCode::IllegalTransition);
    for(std::size_t i=0;i<state_->count;++i)if(!visitor(context,state_->roots[i].object))return Status::failure(FailureCode::GenerationMismatch);
    return Status::success();
}
bool Sc6ReplayGroundDebrisState::RetainedRenderBinding(std::uintptr_t component,const std::array<int,2>& weak,
    unsigned id,std::uintptr_t asset,bool dormant) const noexcept {
    // Missing static proxies require a separate native reconstruction owner.
    // This retained B path does not waive the existing missing-proxy guard.
    if(dormant || !state_ || !state_->lease || !state_->lease->registered())return false;
    const auto found=std::find_if(state_->meshes.begin(),state_->meshes.end(),[&](const auto& m){return m.component==component;});
    if(found==state_->meshes.end() || found->weak!=weak || found->primitive_id!=id || found->asset!=asset
        || !State::live(state_->base,component,weak))return false;
    const State::Root* retained{};
    for(std::size_t i=0;i<state_->count;++i)if(state_->roots[i].object==found->root)retained=&state_->roots[i];
    State::Array ring{};std::uintptr_t member{};
    if(!retained || !State::live(state_->base,retained->object,retained->weak)
        || !State::read(retained->object+0x820,ring) || ring!=retained->ring
        || found->ordinal>=unsigned(ring.count) || !State::read(ring.data+found->ordinal*0x28,member) || member!=component)return false;
    std::uintptr_t root{},owner{},current_asset{},proxy{};unsigned current_id{},visibility{},flags{};
    return State::read(component+0x20,root) && root==found->root && State::read(component+0x190,owner) && owner==found->actor
        && State::read(component+0x920,current_asset) && current_asset==asset
        && State::read(component+0x420,current_id) && current_id==id
        && State::read(component+0x240,visibility) && (visibility&~0x10u)==(found->visibility&~0x10u)
        && State::read(component+0x188,flags) && (flags&~0xc0000060u)==(found->flags&~0xc0000060u)
        && State::read(component+0x790,proxy) && (!dormant || !proxy);
}
Status Sc6ReplayGroundDebrisState::ProbeMotion(std::uintptr_t base,void* battle,void* world,
    std::size_t budget,MotionProbeWitness& witness) noexcept {
    witness={};witness.check="probe_budget";
    constexpr std::size_t scratch=8192+sizeof(State);
    if(budget<=scratch)return Status::failure(FailureCode::CapacityExceeded);
    State identity;identity.base=base;identity.world=reinterpret_cast<std::uintptr_t>(world);
    witness.check="probe_event_identity";
    if(!battle || !State::read(reinterpret_cast<std::uintptr_t>(battle)+0x508,identity.manager)
        || !identity.read_event_name())return Status::failure(FailureCode::IdentityMismatch);
    Sc6ReplayGroundDebrisState inventory;
    auto result=inventory.Prepare(base,battle,world,identity.deactivation_name,budget-scratch);
    if(result.ok() && inventory.empty()) {witness.check="probe_requires_ground";result=Status::failure(FailureCode::UnsupportedContent);}
    if(result.ok())result=inventory.PreparePhysics(budget-scratch);
    witness.owned_bytes=inventory.owned_bytes()+scratch;
    if(result.ok() && witness.owned_bytes>budget)result=Status::failure(FailureCode::CapacityExceeded);
    if(!result.ok())witness.check=inventory.empty()?witness.check:inventory.failed_check();
    // Retained-only physics admission has not removed/submitted anything.
    // Release its inspection packets before native movement dirties the scene.
    if(inventory.prepared() && inventory.RecoverBodies()!=Progress::Complete) {
        witness.check=inventory.failed_check();return Status::failure(FailureCode::UndoFailed);
    }
    if(result.ok()) {
        witness.roots=static_cast<unsigned>(inventory.state_->count);
        witness.meshes=static_cast<unsigned>(inventory.meshes().size());
        std::array<std::uintptr_t,64> objects{};unsigned count{};
        for(const auto& mesh:inventory.meshes())objects[count++]=mesh.component;
        const auto move=[base](std::uintptr_t object,const auto& delta,const auto& quaternion) noexcept {
            __try {
                return reinterpret_cast<bool(*)(void*,const void*,const void*,bool,void*,unsigned,unsigned char)>(base+0x1da43e0)
                    (reinterpret_cast<void*>(object),delta.data(),quaternion.data(),false,nullptr,0,1);
            } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
        };
        witness.check="probe_native_motion";
        if(!ReplayGroundPose::PerturbNative([](auto p,auto& v){return State::read(p,v);},move,
            {objects.data(),count},witness.changed))result=Status::failure(FailureCode::AdvanceFailed);
        if(result.ok()) {
            witness.check="probe_logical_state_changed";
            result=inventory.ValidateFrozen();
        }
    }
    if(inventory.prepared()) {
        const auto released=inventory.ReleaseRecoveredOwners();
        if(!released.ok()) {witness.check="probe_lease_release";return released;}
    }
    if(result.ok())witness.check="complete";
    return result;
}
