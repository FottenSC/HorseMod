// Shared application-entry admission. Scene resolution and weak lookup are supplied
// by the native host; fixtures use real SDK scenes with controlled engine bindings.
    template<class Lookup,class Resolve>
    bool collision_update_domain(std::uintptr_t module,std::span<const std::uintptr_t> children,
        const char*& check,std::uintptr_t& rejected_owner,Lookup lookup,Resolve resolve) noexcept {
        body_inventory={};unrelated_dynamic=false;unrelated_dynamic_actor=0;
        const auto fail=[&](const char* reason){check=reason;rejected_owner=body_diagnostic.actor;return false;};
        // A first birth can occur during the upcoming native application.
        // Empty current membership does not authorize unrelated free bodies.
        std::uintptr_t owner{},pending{};unsigned count{};unsigned char asynchronous{};
        std::array<short,2> ids{};
        if(!read(world+0x1c8,owner) || !owner || !read(owner+0x160,pending) || pending
            || !read(owner+4,count) || count>3 || !read(owner,asynchronous)
            || !read(owner+0xf8,ids[0]) || !read(owner+0xfc,ids[1]))return fail("update_collision_world");
        std::array<bool,64> found{};
        for(unsigned i=0;i<2;++i) {
            if(i==0?!count:(!asynchronous || count<=2))continue;
            if(!lookup(ids[i],scenes[i]))return fail("update_collision_scene");
            SceneLock lock{scenes[i],module};
            if(!lock.Enter())return fail("update_collision_lock");
            std::uintptr_t current_owner{},user{},bound{},resolved{};unsigned tag{};short current_id{};
            if(!read(world+0x1c8,current_owner) || current_owner!=owner
                || !read(owner+0xf8+i*4,current_id) || current_id!=ids[i]
                || !lookup(current_id,resolved) || resolved!=scenes[i]
                || !read(scenes[i]+8,user) || !user || !read(user,tag) || tag!=3
                || !read(user+8,bound) || bound!=owner)return fail("update_collision_scene_identity");
            ReplayPhysicsRemovalGuard aggregates;
            if(!aggregates.ValidateCollisionAggregates([](auto p,auto& v){return read(p,v);},module,scenes[i]))
                return fail("update_collision_aggregates");
            std::array<std::uintptr_t,16> selected{};unsigned selected_count{};
            if(!physics_domain(module,scenes[i],found,selected,selected_count,children,
                resolve,true))return fail(physics_check);
        }
        for(std::size_t i=0;i<children.size();++i)if(!found[i])return fail("update_collision_membership");
        body_inventory.scan_complete=true;
        if(unrelated_dynamic) {body_diagnostic.actor=unrelated_dynamic_actor;return fail("body_unrelated_dynamic");}
        if(!engine_physics_quiescent())return fail("update_collision_queues");
        check="none";rejected_owner=0;return true;
    }
