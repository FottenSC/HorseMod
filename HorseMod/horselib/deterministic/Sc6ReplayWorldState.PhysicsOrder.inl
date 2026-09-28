// Included inside the physics implementation's anonymous namespace.
using PhysicsImage = Sc6ReplayWorldState::PhysicsBoundary;
using PhysicsActor = PhysicsImage::ActorObservation;

bool PhysicsOrderDiffers(const PhysicsImage& a,const PhysicsImage& b,unsigned scene) noexcept {
    if(!PhysicsNodeAllocationEqual(a.node_domains[scene],b.node_domains[scene]))return true;
    for(unsigned i=0;i<a.observed_actors[scene];++i)
        if(a.actors[scene][i].kinematic_admission!=b.actors[scene][i].kinematic_admission
            || ReadAt<unsigned>(a.actors[scene][i].simulation_storage.data(),0xb8)
                !=ReadAt<unsigned>(b.actors[scene][i].simulation_storage.data(),0xb8)) return true;
    return false;
}

const PhysicsActor* PhysicsOrderRoot(const PhysicsImage& image,unsigned scene) noexcept {
    for(unsigned i=0;i<image.observed_actors[scene];++i)
        if(image.actors[scene][i].kinematic_admission.valid) return &image.actors[scene][i];
    return nullptr;
}

// Restore completed scheduling state, never invoke wake/sleep callbacks.
// Membership changes are restricted to independently quiescent, isolated
// kinematics with retained nodes; all other owners remain permutation-only.
bool PhysicsOrderImageValid(const PhysicsImage& image,unsigned scene,const char** check=nullptr) noexcept {
    const auto reject=[&](const char* why){if(check)*check=why;return false;};
    const auto* root=PhysicsOrderRoot(image,scene);if(!root) return reject("physics_order_root");
    const auto& common=root->kinematic_admission;
    const auto count=ReadAt<unsigned>(common.active_header.data(),8);
    const auto capacity=ReadAt<unsigned>(common.active_header.data(),12)&0x7fffffff;
    if(!count || count>64 || count>capacity || !ReadAt<std::uintptr_t>(common.active_header.data(),0)
        || ReadAt<unsigned>(common.active_header.data(),16)!=count
        || common.controller_vtable!=image.module+0x1ab0e8) return reject("physics_order_active_domain");
    unsigned represented{};std::array<unsigned,2> pending_sleep{},pending_wake{};
    for(unsigned i=0;i<image.observed_actors[scene];++i) {
        const auto& actor=image.actors[scene][i];const auto& admission=actor.kinematic_admission;
        if(!admission.valid) {
            const auto id=ReadAt<unsigned>(actor.simulation_storage.data(),0xb0)>>6;
            if(actor.isolated_kinematic && image.node_domains[scene].valid) {
                if(!IsolatedPhysicsKinematic(image,scene,i) || id>=image.node_domains[scene].next_id
                    || ReadAt<unsigned>(actor.simulation_storage.data(),0xb8)!=0xfffffffeu
                    || admission.scene!=common.scene) return reject("physics_order_inactive_kinematic");
                for(unsigned slot=0;slot<2;++slot) {
                    if(PendingPhysicsNodeSleep(image,scene,i,slot))++pending_sleep[slot];
                    else if(image.node_domains[scene].nodes[slot][id][4]!=std::byte{5}
                        || image.node_domains[scene].indices[slot][id]!=0x3ffffffu)
                        return reject("physics_order_inactive_node");
                }
            }
            continue;
        }
        ++represented;
        if(!actor.mutable_projection() || admission.scene!=common.scene
            || admission.controller!=common.controller || admission.controller_owner!=common.controller_owner
            || admission.controller_vtable!=common.controller_vtable || admission.island_owner!=common.island_owner
            || admission.active_header!=common.active_header || admission.active_bodies!=common.active_bodies
            || ReadAt<std::uintptr_t>(actor.simulation_storage.data(),0)!=image.module+0x1aadc0
            || ReadAt<std::uintptr_t>(actor.simulation_storage.data(),0x48)!=actor.actor+0x80) return reject("physics_order_body_binding");
        const auto index=ReadAt<unsigned>(actor.simulation_storage.data(),0xb8);
        const auto node=ReadAt<unsigned>(actor.simulation_storage.data(),0xb0);
        if(index>=count || common.active_bodies[index]!=actor.actor+0x80 || (node>>6)>=4096) return reject("physics_order_body_index");
        for(unsigned prior=0;prior<i;++prior) {
            const auto& other=image.actors[scene][prior];if(!other.kinematic_admission.valid) continue;
            if(other.actor==actor.actor || other.simulation==actor.simulation
                || ReadAt<unsigned>(other.simulation_storage.data(),0xb0)==node
                || ReadAt<unsigned>(other.simulation_storage.data(),0xb8)==index) return reject("physics_order_duplicate_body");
        }
        for(unsigned slot=0;slot<2;++slot) {
            const auto& island=admission.islands[slot];const auto& shared=common.islands[slot];
            const auto nodes_count=ReadAt<unsigned>(island.storage_header.data(),8);
            const auto indices_count=ReadAt<unsigned>(island.storage_header.data(),24);
            const auto island_count=ReadAt<unsigned>(island.list_headers[0].data(),8);
            const auto activation_count=ReadAt<unsigned>(island.list_headers[1].data(),8);
            const bool waking=PendingPhysicsNodeWake(image,scene,i,slot);
            if(waking)++pending_wake[slot];
            if(island.storage_header!=shared.storage_header || island.list_headers!=shared.list_headers
                || island.lists!=shared.lists || !ReadAt<std::uintptr_t>(island.storage_header.data(),0x10)
                || !ReadAt<std::uintptr_t>(island.storage_header.data(),0)
                || (node>>6)>=nodes_count || (node>>6)>=indices_count
                || nodes_count>(ReadAt<unsigned>(island.storage_header.data(),12)&0x7fffffff)
                || indices_count>(ReadAt<unsigned>(island.storage_header.data(),28)&0x7fffffff)
                || (!waking && (std::to_integer<unsigned>(island.node[4])&0x67)!=6)
                || island_count>64
                || (ReadAt<unsigned>(island.list_headers[0].data(),12)&0x7fffffff)<island_count
                || (island_count && !ReadAt<std::uintptr_t>(island.list_headers[0].data(),0))
                || activation_count>64 || activation_count>(ReadAt<unsigned>(island.list_headers[1].data(),12)&0x7fffffff)
                || (activation_count && !ReadAt<std::uintptr_t>(island.list_headers[1].data(),0))
                || island.index>=(waking?activation_count:island_count)
                || island.lists[waking?1:0][island.index]!=node) return reject("physics_order_island_domain");
        }
    }
    if(represented!=count)return reject("physics_order_unrepresented_body");
    for(unsigned slot=0;slot<2;++slot)
        if(ReadAt<unsigned>(common.islands[slot].list_headers[0].data(),8)!=represented-pending_wake[slot]+pending_sleep[slot]
            || ReadAt<unsigned>(common.islands[slot].list_headers[1].data(),8)!=pending_wake[slot])
            return reject("physics_order_unrepresented_island_node");
    return true;
}

bool PhysicsOrderPairValid(const PhysicsImage& a,const PhysicsImage& b,unsigned scene,const char** check=nullptr) noexcept {
    const auto reject=[&](const char* why){if(check)*check=why;return false;};
    if(!PhysicsOrderImageValid(a,scene,check) || !PhysicsOrderImageValid(b,scene,check)) return false;
    const bool nodes=a.node_domains[scene].valid || b.node_domains[scene].valid;
    if(nodes && !PhysicsNodePairValid(a,b,scene))return reject("physics_node_ownership");
    auto common=PhysicsOrderRoot(a,scene)->kinematic_admission;
    const auto& current=PhysicsOrderRoot(b,scene)->kinematic_admission;
    // Active counts and inverse arrays may change; their native backing and
    // controller ownership must remain stable. Node/index vectors alone may
    // relocate through the existing validated binding path.
    std::memcpy(common.active_header.data()+8,current.active_header.data()+8,4);
    std::memcpy(common.active_header.data()+16,current.active_header.data()+16,4);
    if(common.scene!=current.scene || common.controller!=current.controller
        || common.controller_owner!=current.controller_owner || common.controller_vtable!=current.controller_vtable
        || common.island_owner!=current.island_owner || common.active_header!=current.active_header)
        return reject("physics_order_common_binding");
    for(unsigned i=0;i<a.observed_actors[scene];++i) {
        auto normalized=a.actors[scene][i].kinematic_admission;
        const auto& other=b.actors[scene][i].kinematic_admission;
        if(normalized.valid!=other.valid) {
            if(!IsolatedPhysicsKinematic(a,scene,i) || !IsolatedPhysicsKinematic(b,scene,i)
                || a.actors[scene][i].actor!=b.actors[scene][i].actor
                || a.actors[scene][i].simulation!=b.actors[scene][i].simulation
                || normalized.scene!=other.scene) return reject("physics_order_active_membership");
            // Complete node ownership/activity was independently checked above.
            continue;
        }
        if(!normalized.valid) {if(normalized!=other)return reject("physics_order_inactive_owner");continue;}
        if(a.actors[scene][i].actor!=b.actors[scene][i].actor
            || a.actors[scene][i].simulation!=b.actors[scene][i].simulation
            || ReadAt<unsigned>(a.actors[scene][i].simulation_storage.data(),0xb0)
                !=ReadAt<unsigned>(b.actors[scene][i].simulation_storage.data(),0xb0)) return reject("physics_order_changed_body");
        normalized.active_bodies=other.active_bodies;
        std::memcpy(normalized.active_header.data()+8,other.active_header.data()+8,4);
        std::memcpy(normalized.active_header.data()+16,other.active_header.data()+16,4);
        for(unsigned slot=0;slot<2;++slot) {
            normalized.islands[slot].index=other.islands[slot].index;
            normalized.islands[slot].lists=other.islands[slot].lists;
            for(unsigned list=0;list<2;++list)
                normalized.islands[slot].list_headers[list]=other.islands[slot].list_headers[list];
            if(nodes)normalized.islands[slot].node[4]=other.islands[slot].node[4];
            // Node/index backing is process-local. Both complete images
            // validate the retained handles and logical extent independently.
            // Extent changes additionally require the complete retired-node
            // domain proof. Never restore a backing pointer or vector length.
            for(const auto offset:{0u,16u}) {
                std::memcpy(normalized.islands[slot].storage_header.data()+offset,other.islands[slot].storage_header.data()+offset,8);
                std::memcpy(normalized.islands[slot].storage_header.data()+offset+12,other.islands[slot].storage_header.data()+offset+12,4);
                if(nodes)std::memcpy(normalized.islands[slot].storage_header.data()+offset+8,other.islands[slot].storage_header.data()+offset+8,4);
            }
        }
        if(normalized!=other) return reject("physics_order_changed_binding_or_node");
    }
    return true;
}

bool PhysicsOrderWritable(const PhysicsImage& image,unsigned scene,const PhysicsImage& target) noexcept {
    const auto* root=PhysicsOrderRoot(image,scene);if(!root)return false;
    const auto& common=root->kinematic_admission;
    const auto* desired=PhysicsOrderRoot(target,scene);if(!desired)return false;
    const auto count=std::max(ReadAt<unsigned>(common.active_header.data(),8),ReadAt<unsigned>(desired->kinematic_admission.active_header.data(),8));
    if(count>(ReadAt<unsigned>(common.active_header.data(),12)&0x7fffffff)
        || !Writable(reinterpret_cast<void*>(common.scene+0x28),4) || !Writable(reinterpret_cast<void*>(common.scene+0x30),4)
        || !Writable(reinterpret_cast<void*>(ReadAt<std::uintptr_t>(common.active_header.data(),0)),count*8))return false;
    for(unsigned slot=0;slot<2;++slot)for(unsigned list=0;list<2;++list) {
        const auto island_count=std::max(ReadAt<unsigned>(common.islands[slot].list_headers[list].data(),8),
            ReadAt<unsigned>(desired->kinematic_admission.islands[slot].list_headers[list].data(),8));
        if(island_count>(ReadAt<unsigned>(common.islands[slot].list_headers[list].data(),12)&0x7fffffff)
            || !Writable(reinterpret_cast<void*>(common.island_owner+(slot?0x310:0xb0)+(list?0x198:0xa0)),4)
            || (island_count && !Writable(reinterpret_cast<void*>(ReadAt<std::uintptr_t>(common.islands[slot].list_headers[list].data(),0)),island_count*4)))return false;
    }
    for(unsigned i=0;i<image.observed_actors[scene];++i) {
        const auto& actor=image.actors[scene][i];
        if(!actor.kinematic_admission.valid && !actor.isolated_kinematic && !target.actors[scene][i].kinematic_admission.valid)continue;
        if(!Writable(reinterpret_cast<void*>(actor.simulation+0xb8),4))return false;
        const auto node=ReadAt<unsigned>(actor.simulation_storage.data(),0xb0)>>6;
        for(const auto& island:common.islands)
            if(!Writable(reinterpret_cast<void*>(ReadAt<std::uintptr_t>(island.storage_header.data(),0x10)+node*4),4)
                || !Writable(reinterpret_cast<void*>(ReadAt<std::uintptr_t>(island.storage_header.data(),0)+node*0x20+4),1))return false;
    }
    return true;
}

bool PhysicsOrderLiveMatches(const PhysicsImage& image,unsigned scene,const PhysicsImage* bindings=nullptr) noexcept {
    __try {
        const auto* root=PhysicsOrderRoot(image,scene);if(!root)return false;
        const auto& common=root->kinematic_admission;
        const auto* binding_root=PhysicsOrderRoot(bindings?*bindings:image,scene);if(!binding_root)return false;
        const auto& binding=binding_root->kinematic_admission;
        const auto count=ReadAt<unsigned>(common.active_header.data(),8);
        if(!image.notifications[scene].valid && image.node_domains[scene].notifications_quiescent
            && (ReadAt<unsigned>(reinterpret_cast<void*>(common.scene),0x1080+0x34)
                || ReadAt<unsigned>(reinterpret_cast<void*>(common.scene),0x10b8+0x34)))return false;
        if(std::memcmp(reinterpret_cast<void*>(common.scene+0x20),common.active_header.data(),0x18)
            || std::memcmp(reinterpret_cast<void*>(ReadAt<std::uintptr_t>(common.active_header.data(),0)),common.active_bodies.data(),count*8))return false;
        for(unsigned i=0;i<image.observed_actors[scene];++i) {
            const auto& actor=image.actors[scene][i];
            if(!actor.kinematic_admission.valid && !actor.isolated_kinematic)continue;
            const auto node=ReadAt<unsigned>(actor.simulation_storage.data(),0xb0)>>6;
            if(ReadAt<unsigned>(reinterpret_cast<void*>(actor.simulation),0xb8)!=ReadAt<unsigned>(actor.simulation_storage.data(),0xb8))return false;
            for(unsigned slot=0;slot<2;++slot) {
                const auto& island=common.islands[slot];
                const auto& storage=binding.islands[slot].storage_header;
                const auto& expected_node=actor.kinematic_admission.valid?actor.kinematic_admission.islands[slot].node:image.node_domains[scene].nodes[slot][node];
                const auto expected_index=actor.kinematic_admission.valid?actor.kinematic_admission.islands[slot].index:image.node_domains[scene].indices[slot][node];
                const auto native=common.island_owner+(slot?0x310:0xb0);
                if(std::memcmp(reinterpret_cast<void*>(native+0x18),storage.data(),0x20)
                    || std::memcmp(reinterpret_cast<void*>(ReadAt<std::uintptr_t>(storage.data(),0)+node*0x20),expected_node.data(),0x20)
                    || ReadAt<unsigned>(reinterpret_cast<void*>(ReadAt<std::uintptr_t>(storage.data(),0x10)),node*4)!=expected_index)return false;
                for(unsigned list=0;list<2;++list) {
                    auto header=binding.islands[slot].list_headers[list];
                    const auto island_count=ReadAt<unsigned>(island.list_headers[list].data(),8);
                    std::memcpy(header.data()+8,&island_count,4);
                    if(std::memcmp(reinterpret_cast<void*>(native+(list?0x190:0x98)),header.data(),0x10)
                        || (island_count && std::memcmp(reinterpret_cast<void*>(ReadAt<std::uintptr_t>(header.data(),0)),
                            island.lists[list].data(),island_count*4)))return false;
                }
            }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool WritePhysicsOrder(const PhysicsImage& image,unsigned scene,const PhysicsImage* bindings=nullptr) noexcept {
    __try {
        const auto& common=PhysicsOrderRoot(image,scene)->kinematic_admission;
        const auto& binding=PhysicsOrderRoot(bindings?*bindings:image,scene)->kinematic_admission;
        const auto count=ReadAt<unsigned>(common.active_header.data(),8);
        std::memcpy(reinterpret_cast<void*>(ReadAt<std::uintptr_t>(common.active_header.data(),0)),common.active_bodies.data(),count*8);
        for(unsigned slot=0;slot<2;++slot)for(unsigned list=0;list<2;++list) {
            const auto island_count=ReadAt<unsigned>(common.islands[slot].list_headers[list].data(),8);
            if(island_count)std::memcpy(reinterpret_cast<void*>(ReadAt<std::uintptr_t>(binding.islands[slot].list_headers[list].data(),0)),common.islands[slot].lists[list].data(),island_count*4);
        }
        for(unsigned i=0;i<image.observed_actors[scene];++i) {
            const auto& actor=image.actors[scene][i];
            if(!actor.kinematic_admission.valid && !actor.isolated_kinematic)continue;
            std::memcpy(reinterpret_cast<void*>(actor.simulation+0xb8),actor.simulation_storage.data()+0xb8,4);
            const auto node=ReadAt<unsigned>(actor.simulation_storage.data(),0xb0)>>6;
            for(unsigned slot=0;slot<2;++slot) {
                const auto& storage=binding.islands[slot].storage_header;
                const auto index=actor.kinematic_admission.valid?actor.kinematic_admission.islands[slot].index:image.node_domains[scene].indices[slot][node];
                const auto flags=actor.kinematic_admission.valid?actor.kinematic_admission.islands[slot].node[4]:image.node_domains[scene].nodes[slot][node][4];
                std::memcpy(reinterpret_cast<void*>(ReadAt<std::uintptr_t>(storage.data(),0x10)+node*4),&index,4);
                std::memcpy(reinterpret_cast<void*>(ReadAt<std::uintptr_t>(storage.data(),0)+node*0x20+4),&flags,1);
            }
        }
        // Publish extents only after the arrays and inverse mappings agree.
        std::memcpy(reinterpret_cast<void*>(common.scene+0x28),&count,4);
        std::memcpy(reinterpret_cast<void*>(common.scene+0x30),&count,4);
        for(unsigned slot=0;slot<2;++slot)for(unsigned list=0;list<2;++list) {
            const auto island_count=ReadAt<unsigned>(common.islands[slot].list_headers[list].data(),8);
            std::memcpy(reinterpret_cast<void*>(common.island_owner+(slot?0x310:0xb0)+(list?0x198:0xa0)),&island_count,4);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
