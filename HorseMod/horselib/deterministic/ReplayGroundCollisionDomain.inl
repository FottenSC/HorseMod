// Shared production predicate; caller owns current scene locking and aggregate proof.
    template<class Resolve> bool physics_domain(std::uintptr_t module,std::uintptr_t scene,std::array<bool,64>& found,
        std::array<std::uintptr_t,16>& selected,unsigned& selected_count,
        std::span<const std::uintptr_t> children,Resolve resolve,bool complete_inventory=false) noexcept {
        // Read-only consumer admission, before any body removal. Collider
        // observations select/reject ownership; none become simulation input.
        struct Filter {std::uintptr_t actor{};unsigned attributes{};std::array<unsigned,4> data{};bool selected{};};
        std::array<Filter,512> filters{};unsigned filter_count{};
        __try {
            const auto ptr=[](std::uintptr_t p,unsigned o=0){return *reinterpret_cast<const std::uintptr_t*>(p+o);};
            const auto u32=[](std::uintptr_t p,unsigned o=0){return *reinterpret_cast<const unsigned*>(p+o);};
            const auto sc=scene+0x10;
            physics_check="scene_type";if(ptr(scene)!=module+0x19d008)return false;
            physics_check="scene_phase";if(u32(scene,0x1f14))return false;
            physics_check="scene_filter_shader";if(ptr(sc,0x1070)!=base+0x204cf60)return false;
            physics_check="scene_filter_callback";if(ptr(sc,0x1078))return false;
            for(unsigned slot=0;slot<2;++slot) {
                physics_check=slot?"scene_wake_notifications":"scene_sleep_notifications";
                const auto queue=sc+(slot?0x10b8:0x1080);
                if(!u32(queue,0x34))continue;
                ReplayPhysicsNotifications::Storage storage;
                if(!ReplayPhysicsNotifications::ReadStorage(queue,storage,[](auto p,auto& v){return read(p,v);}))return false;
                // Bounded evidence under the scene lock. Complete queue ownership
                // and callback admission follow the body inventory below.
                auto& n=notification_diagnostic;n={};n.scene=scene;n.slot=slot;n.count=u32(queue,0x34);
                const auto dense=ptr(queue,8),actors=ptr(scene,0x2590);const auto actors_count=u32(scene,0x2598);
                if(!dense || actors_count>64 || (actors_count&&!actors))return false;
                n.core=ptr(dense);
                for(unsigned i=0;i<actors_count;++i) {
                    const auto actor=ptr(actors,i*8);
                    if(!actor || actor+0x80!=n.core || ptr(actor)!=module+0x19b7c0)continue;
                    n.actor=actor;n.simulation=ptr(n.core);
                    if(!n.simulation || ptr(n.simulation)!=module+0x1aadc0
                        || ptr(n.simulation,0x40)!=sc || ptr(n.simulation,0x48)!=n.core)return false;
                    n.owned=true;n.client=*reinterpret_cast<const unsigned char*>(n.core+0xb);
                    n.actor_flags=*reinterpret_cast<const unsigned char*>(n.core+0xc);
                    n.simulation_flags=*reinterpret_cast<const unsigned short*>(n.simulation+0xb4);
                    const auto clients=ptr(sc,0x10f8);const auto client_count=u32(sc,0x1100);
                    if(!clients || n.client>=client_count || client_count>16)return false;
                    const auto client=ptr(clients,n.client*8);
                    if(client) {n.callback=ptr(client,0x48);if(n.callback)n.callback_type=ptr(n.callback);}
                    break;
                }
                break;
            }
            physics_check="scene_filter_signature";
            std::uint64_t shader_hash=14695981039346656037ull;
            for(unsigned i=0;i<704;++i)shader_hash=(shader_hash^*reinterpret_cast<const unsigned char*>(base+0x204cf60+i))*1099511628211ull;
            if(shader_hash!=0x9a921a6ea4013b30ull)return false;
            physics_check="scene_actor_inventory";
            const auto table=ptr(scene,0x2590);const auto count=u32(scene,0x2598);
            if(count>64 || (count&&!table))return false;
            physics_check="body_consumers";
            for(unsigned i=0;i<count;++i) {
                body_diagnostic={};body_diagnostic.actor_index=i;
                const auto actor=ptr(table,i*8),type=ptr(actor);
                body_diagnostic.actor=actor;body_diagnostic.type=type;
                physics_check="body_type";
                if(type!=module+0x19b7c0 && type!=module+0x19be60)return false;
                if(type==module+0x19be60) {
                    physics_check="body_inventory_capacity";
                    if(!body_inventory.Add({scene,actor,0,0,type,0,0,
                            ReplayPhysicsBodyInventory::Role::Static,-1,0,i}))return false;
                    continue;
                }
                alignas(16) std::array<std::byte,0xe8> values{};
                reinterpret_cast<void*(*)(void*,const void*)>(module+0x7810)(values.data(),reinterpret_cast<void*>(actor));
                std::uintptr_t component{},body_instance{};const auto user=ptr(reinterpret_cast<std::uintptr_t>(values.data()),0x20);
                if(user && u32(user)==1) {
                    body_instance=ptr(user,8);
                    if(body_instance)component=resolve(body_instance+0x138);
                }
                const auto mesh_count=children.empty()?meshes.size():children.size();
                std::size_t mesh=mesh_count;
                for(std::size_t m=0;m<mesh_count;++m)if((children.empty()?meshes[m].component:children[m])==component)mesh=m;
                body_diagnostic.component=component;body_diagnostic.body=body_instance;
                body_diagnostic.mesh_index=static_cast<unsigned>(mesh);
                body_diagnostic.body_flags=std::to_integer<unsigned>(values[0x9c]);
                body_diagnostic.actor_flags=std::to_integer<unsigned>(values[0x10]);
                const auto identity=component_identity(component);
                physics_check="body_inventory_capacity";
                if(!body_inventory.Add({scene,actor,body_instance,component,type,
                        body_diagnostic.body_flags,body_diagnostic.actor_flags,
                        mesh==mesh_count?ReplayPhysicsBodyInventory::Role::UnrelatedDynamic
                                        :ReplayPhysicsBodyInventory::Role::GroundChild,
                        identity[0],identity[1],i}))return false;
                if(mesh==mesh_count) {
                    // No unrelated authoritative free body can receive an
                    // impulse from the reconstructed motion domain.
                    if(!(std::to_integer<unsigned>(values[0x9c])&1)) {
                        if(!unrelated_dynamic)unrelated_dynamic_actor=actor;
                        unrelated_dynamic=true;
                        physics_check="body_unrelated_dynamic";
                        if(!complete_inventory)return false;
                    }
                    continue;
                }
                physics_check="body_selection";
                if(found[mesh] || selected_count==selected.size())return false;
                physics_check="body_flags";
                if(!ReplayGroundBodyAdmission::Flags([](auto address,auto& value){return read(address,value);},
                    actor,body_diagnostic.body_flags,body_diagnostic.actor_flags))return false;
                physics_check="body_constraint_entry";
                if(ptr(type,0xd8)!=module+0x3c860)return false;
                physics_check="body_constraints";
                body_diagnostic.constraints=reinterpret_cast<unsigned(*)(void*)>(module+0x3c860)(reinterpret_cast<void*>(actor));
                if(body_diagnostic.constraints)return false;
                physics_check="body_instance_consumers";
                if(body_instance!=component+0x430 || ptr(body_instance,0x100) || ptr(body_instance,0x210)
                    || ptr(body_instance,0xb0) || body_proof_count==body_proofs.size())return false;
                // Every shape is simulation-only or disabled. No scene query,
                // trigger, joint, or sleep-notification consumer is admitted.
                const auto shapes=*reinterpret_cast<const unsigned short*>(actor+0x30);
                physics_check="body_shape_capacity";
                if(shapes>32)return false;
                const auto handles=*reinterpret_cast<const short*>(actor+0x40)==1?actor+0x38:ptr(actor,0x38);
                const auto shape_table=shapes==1?actor+0x28:ptr(actor,0x28);
                physics_check="body_shape_arrays";
                if(shapes && (!handles || !shape_table))return false;
                for(unsigned h=0;h<shapes;++h) {
                    const auto shape=ptr(shape_table,h*8);
                    body_diagnostic.shape_index=h;body_diagnostic.shape=shape;
                    physics_check="body_shape_identity";if(!shape)return false;
                    body_diagnostic.query_handle=ptr(handles,h*8);
                    body_diagnostic.shape_flags=u32(shape,0x38);
                    body_diagnostic.core_flags=*reinterpret_cast<const unsigned char*>(shape+0x90);
                    physics_check="body_shape_query_handle";if(body_diagnostic.query_handle!=0xffffffffu)return false;
                    physics_check="body_shape_consumers";
                    if((body_diagnostic.shape_flags&0x45) || (body_diagnostic.core_flags&6))return false;
                }
                found[mesh]=true;selected[selected_count++]=actor;
                body_proofs[body_proof_count++]={actor,body_instance};
            }
            if(u32(sc,0x1080+0x34) || u32(sc,0x10b8+0x34)) {
                ReplayPhysicsNotifications notifications;
                physics_check="scene_notification_ownership";
                if(!notifications.Capture(sc,[](auto p,auto& v){return read(p,v);},[&](std::uintptr_t core) {
                    unsigned matches{};
                    for(unsigned i=0;i<count;++i) {
                        const auto actor=ptr(table,i*8);
                        if(!actor || actor+0x80!=core || ptr(actor)!=module+0x19b7c0)continue;
                        const auto sim=ptr(core);
                        if(!sim || ptr(sim)!=module+0x1aadc0 || ptr(sim,0x40)!=sc || ptr(sim,0x48)!=core)return false;
                        ++matches;
                    }
                    return matches==1;
                }))return false;
                for(unsigned i=0;i<count;++i) {
                    const auto actor=ptr(table,i*8);if(ptr(actor)!=module+0x19b7c0)continue;
                    const auto sim=ptr(actor,0x80);if(!sim)return false;
                    const auto flags=*reinterpret_cast<const unsigned short*>(sim+0xb4);
                    const auto membership=notifications.Membership(actor+0x80);
                    physics_check="scene_notification_flags";
                    if((flags&0x30)!=membership || (flags&0xc0)==0xc0
                        || ((flags&0x40)&&!(membership&0x10)) || ((flags&0x80)&&!(membership&0x20)))return false;
                    if(!membership)continue;
                    physics_check="scene_notification_debris";
                    for(unsigned j=0;j<selected_count;++j)if(selected[j]==actor)return false;
                    // Shipped E7670 tests BodyCore+0xC bit4 before collecting
                    // an actor for callback delivery. Internal wake bookkeeping
                    // without that bit remains native and is neither cleared nor
                    // dispatched here. Changed callback flags still reject.
                    physics_check="scene_notification_callback";
                    if(*reinterpret_cast<const unsigned char*>(actor+0x8c)&4)return false;
                }
            }
            if(!selected_count) {physics_check="none";return true;}
            physics_check="contact_consumers";
            const auto aabb=ptr(scene,0x738),volumes=ptr(aabb,0xa8);const auto extent=u32(aabb,0x1c0);
            if(extent>ReplayPhysicsRemovalGuard::max_handles || (extent&&!volumes)
                || ptr(module+0x1aae40,8)!=module+0x1143e0)return false;
            for(unsigned h=0;h<extent;++h) {
                const auto shape=ptr(volumes,h*16);
                if(!shape || u32(volumes,h*16+8)!=0xffffffffu)continue; // Both callers separately prove empty aggregate ownership.
                if(filter_count==filters.size() || ptr(shape)!=module+0x1aae40)return false;
                auto& f=filters[filter_count++];const auto sim=ptr(shape,0x10);
                f.actor=sim;
                for(unsigned j=0;j<selected_count;++j)f.selected|=sim==ptr(selected[j],0x80);
                reinterpret_cast<void(*)(void*,unsigned*,void*)>(module+0x1143e0)(reinterpret_cast<void*>(shape),&f.attributes,f.data.data());
            }
            for(unsigned i=0;i<filter_count;++i)for(unsigned j=0;j<i;++j) {
                const auto& a=filters[i];const auto& b=filters[j];
                if((!a.selected&&!b.selected) || a.actor==b.actor)continue;
                unsigned short result{},pairs{};
                const auto returned=reinterpret_cast<unsigned short*(*)(unsigned short*,unsigned,const void*,unsigned,const void*,unsigned short*,const void*,unsigned)>(base+0x204cf60)(
                    &result,a.attributes,a.data.data(),b.attributes,b.data.data(),&pairs,reinterpret_cast<void*>(ptr(sc,0x1060)),u32(sc,0x1068));
                if(returned!=&result || (result!=0 && result!=1 && result!=2) || (!result && pairs!=0x401))return false;
            }
            physics_check="none";return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
