// Native node handles are simulation continuation; backing/vector extent is not.
using NodeImage = Sc6ReplayWorldState::PhysicsBoundary;
using NodeDomain = NodeImage::NodeDomain;
bool IsolatedPhysicsKinematic(const NodeImage& image,unsigned scene,unsigned actor) noexcept;

bool PendingPhysicsNodeWake(const NodeImage& image,unsigned scene,unsigned actor,unsigned slot) noexcept {
    const auto& row=image.actors[scene][actor];const auto& d=image.node_domains[scene];
    const auto handle=ReadAt<unsigned>(row.simulation_storage.data(),0xb0),id=handle>>6;
    if(!row.kinematic_admission.valid || !IsolatedPhysicsKinematic(image,scene,actor)
        || !d.island_ids_valid || !d.activation_domain_clear[slot] || id>=d.next_id
        || id>=d.island_id_counts[slot] || d.island_ids[slot][id]!=0xffffffffu
        || d.nodes[slot][id][4]!=std::byte{0x24})return false;
    const auto& node=d.nodes[slot][id];
    if(ReadAt<unsigned>(node.data(),0)!=0xffffffffu || ReadAt<unsigned>(node.data(),8)!=0xffffffc0u
        || ReadAt<unsigned>(node.data(),12)!=0xffffffc0u || ReadAt<unsigned short>(node.data(),6)
        || ReadAt<unsigned>(node.data(),0x10))return false;
    const auto& island=row.kinematic_admission.islands[slot];
    const auto count=ReadAt<unsigned>(island.list_headers[1].data(),8);
    const auto index=d.indices[slot][id];
    return row.kinematic_admission.island_owner==d.owner && count<=64 && index<count
        && island.lists[1][index]==handle;
}

// Native112D60 removes the BodySim independently of147490's island sleep
// request. Flags7 retains the island active-list entry until native processing.
bool PendingPhysicsNodeSleep(const NodeImage& image,unsigned scene,unsigned actor,unsigned slot) noexcept {
    const auto& row=image.actors[scene][actor];const auto& domain=image.node_domains[scene];
    const auto handle=ReadAt<unsigned>(row.simulation_storage.data(),0xb0),id=handle>>6;
    if(row.kinematic_admission.valid || !IsolatedPhysicsKinematic(image,scene,actor)
        || ReadAt<unsigned>(row.simulation_storage.data(),0xb8)!=0xfffffffeu
        || id>=domain.next_id || domain.nodes[slot][id][4]!=std::byte{7})return false;
    for(unsigned i=0;i<image.observed_actors[scene];++i) {
        const auto& admission=image.actors[scene][i].kinematic_admission;
        if(!admission.valid)continue;
        const auto& island=admission.islands[slot];const auto count=ReadAt<unsigned>(island.list_headers[0].data(),8);
        const auto index=domain.indices[slot][id];
        return admission.island_owner==domain.owner && admission.scene==row.kinematic_admission.scene
            && count<=64 && index<count && island.lists[0][index]==handle;
    }
    return false;
}

bool DeletedPhysicsNode(const std::array<std::byte,0x20>& node,unsigned index) noexcept {
    return node[4]==std::byte{8} && ReadAt<unsigned>(node.data(),0)==0xffffffffu
        && ReadAt<unsigned>(node.data(),8)==0xffffffc0u && ReadAt<unsigned>(node.data(),12)==0xffffffc0u
        && !ReadAt<unsigned short>(node.data(),6) && !ReadAt<unsigned>(node.data(),0x10)
        && !ReadAt<std::uintptr_t>(node.data(),0x18) && index==0x3ffffffu;
}

bool PhysicsNodeDomainValid(const NodeImage& image,unsigned scene) noexcept {
    const auto& d=image.node_domains[scene];if(!d.valid)return false;
    if(!d.owner || d.pending_count || d.next_id>64 || d.free_count>d.free_capacity
        || d.free_count>d.next_id || (d.free_count && !d.free_storage))return false;
    std::array<bool,64> free{};
    for(unsigned i=0;i<d.free_count;++i) {
        const auto id=d.free_ids[i];if(id>=d.next_id || free[id])return false;free[id]=true;
    }
    for(unsigned slot=0;slot<2;++slot) {
        if(d.counts[slot]<d.next_id || d.counts[slot]>64)return false;
        if(d.island_ids_valid && (d.island_id_counts[slot]<d.counts[slot] || d.island_id_counts[slot]>64))return false;
        for(unsigned id=0;id<d.counts[slot];++id) {
            const auto& node=d.nodes[slot][id];
            if(id>=d.next_id || free[id]) {
                if(!DeletedPhysicsNode(node,d.indices[slot][id]))return false;
                continue;
            }
            bool found{};
            for(unsigned i=0;i<image.observed_actors[scene];++i) {
                const auto& actor=image.actors[scene][i];
                if(actor.simulation && ReadAt<unsigned>(actor.simulation_storage.data(),0xb0)==id*64
                    && ReadAt<std::uintptr_t>(node.data(),0x18)==actor.simulation+0x60) {
                    const auto flags=std::to_integer<unsigned>(node[4]);
                    if(found || (flags&0x48) || ((flags&0x20) && !PendingPhysicsNodeWake(image,scene,i,slot)))return false;
                    if(actor.kinematic_admission.valid) {
                        const auto& observed=actor.kinematic_admission.islands[slot];
                        if(actor.kinematic_admission.island_owner!=d.owner || observed.node!=node
                            || observed.index!=d.indices[slot][id]
                            || ReadAt<unsigned>(observed.storage_header.data(),8)!=d.counts[slot])return false;
                    } else if(node[4]==std::byte{7}
                        ? !PendingPhysicsNodeSleep(image,scene,i,slot)
                        : d.indices[slot][id]!=0x3ffffffu)return false;
                    found=true;
                }
            }
            if(!found)return false;
        }
    }
    return true;
}

bool ReadPhysicsNodeDomain(NodeImage& image,unsigned scene) noexcept {
    auto& d=image.node_domains[scene];d={};
    for(unsigned i=0;i<image.observed_actors[scene];++i) {
        const auto& admission=image.actors[scene][i].kinematic_admission;
        if(!admission.valid)continue;
        if(d.owner && d.owner!=admission.island_owner)return false;
        d.owner=admission.island_owner;
    }
    if(!d.owner)return true;
    __try {
        const auto* owner=reinterpret_cast<void*>(d.owner);
        d.free_storage=ReadAt<std::uintptr_t>(owner,0);d.free_count=ReadAt<unsigned>(owner,8);
        d.free_capacity=ReadAt<unsigned>(owner,12)&0x7fffffff;d.next_id=ReadAt<unsigned>(owner,16);
        d.pending_count=ReadAt<unsigned>(owner,0x38);
        if(d.free_count>64 || d.free_count>d.free_capacity || d.next_id>64 || (d.free_count && !d.free_storage))return false;
        if(d.free_count)std::memcpy(d.free_ids.data(),reinterpret_cast<void*>(d.free_storage),d.free_count*4);
        for(unsigned slot=0;slot<2;++slot) {
            const auto* island=reinterpret_cast<void*>(d.owner+(slot?0x310:0xb0));
            const auto nodes=ReadAt<std::uintptr_t>(island,0x18),indices=ReadAt<std::uintptr_t>(island,0x28);
            const auto count=ReadAt<unsigned>(island,0x20),index_count=ReadAt<unsigned>(island,0x30);
            if(count>64 || count>(ReadAt<unsigned>(island,0x24)&0x7fffffff) || count!=index_count
                || index_count>(ReadAt<unsigned>(island,0x34)&0x7fffffff) || (count && (!nodes || !indices)))return false;
            d.counts[slot]=count;
            if(count) {
                std::memcpy(d.nodes[slot].data(),reinterpret_cast<void*>(nodes),count*0x20);
                std::memcpy(d.indices[slot].data(),reinterpret_cast<void*>(indices),count*4);
            }
            const auto ids=ReadAt<std::uintptr_t>(island,0xf0);
            const auto ids_count=ReadAt<unsigned>(island,0xf8);
            if(ids_count<count || ids_count>64 || ids_count>(ReadAt<unsigned>(island,0xfc)&0x7fffffff)
                || (ids_count && !ids))return false;
            d.island_id_counts[slot]=ids_count;
            if(ids_count)std::memcpy(d.island_ids[slot].data(),reinterpret_cast<void*>(ids),ids_count*4);
            d.activation_domain_clear[slot]=!ReadAt<unsigned>(island,0x160)
                && !ReadAt<unsigned>(island,0x170) && !ReadAt<unsigned>(island,0x1a8);
            const auto bits=ReadAt<std::uintptr_t>(island,0x178);
            const auto words=ReadAt<unsigned>(island,0x180)&0x7fffffff;
            if(words>128 || (words && !bits))return false;
            for(unsigned word=0;word<words;++word)
                if(ReadAt<unsigned>(reinterpret_cast<void*>(bits),word*4))d.activation_domain_clear[slot]=false;
        }
        d.island_ids_valid=true;
        // 0EC190/0EC240 enqueue Core pointers in these sets (+34 live
        // count, 0F9170). Do not synthesize or replay wake/sleep callbacks.
        const auto* root= [&]() -> const NodeImage::ActorObservation* {
            for(unsigned i=0;i<image.observed_actors[scene];++i)
                if(image.actors[scene][i].kinematic_admission.valid)return &image.actors[scene][i];
            return nullptr;
        }();
        if(root) {
            const auto* sc=reinterpret_cast<void*>(root->kinematic_admission.scene);
            d.notifications_quiescent=!ReadAt<unsigned>(sc,0x1080+0x34) && !ReadAt<unsigned>(sc,0x10b8+0x34);
            d.filter_shader=ReadAt<std::uintptr_t>(sc,0x1070);
            d.filter_callback=ReadAt<std::uintptr_t>(sc,0x1078);
            d.filter_data=ReadAt<std::uintptr_t>(sc,0x1060);
            d.filter_data_size=ReadAt<unsigned>(sc,0x1068);
        }
        d.valid=true;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool IsolatedPhysicsCore(const NodeImage::ActorObservation& row,std::uintptr_t module) noexcept {
    return row.mutable_projection() && !row.interaction_count && row.simulation && row.kinematic
        // Native114BC0 reads the adjacent +AD separately as pose identity.
        && ReadAt<unsigned char>(row.dynamic_storage.data(),0xac)==3
        && ReadAt<std::uintptr_t>(row.simulation_storage.data(),0)==module+0x1aadc0
        && !ReadAt<std::uintptr_t>(row.simulation_storage.data(),0x80)
        && !ReadAt<std::uintptr_t>(row.simulation_storage.data(),0xc0);
}

bool IsolatedPhysicsKinematic(const NodeImage& image,unsigned scene,unsigned actor) noexcept {
    const auto& row=image.actors[scene][actor];
    return (image.notifications[scene].valid
            ? PhysicsNotificationsOwned(image,scene,image.notifications[scene])
            : image.node_domains[scene].notifications_quiescent
                && !(ReadAt<unsigned short>(row.simulation_storage.data(),0xb4)&0xf0)) && row.isolated_kinematic
        && IsolatedPhysicsCore(row,image.module);
}

bool PhysicsNodePairValid(const NodeImage& a,const NodeImage& b,unsigned scene) noexcept {
    const auto& x=a.node_domains[scene];const auto& y=b.node_domains[scene];
    if(!PhysicsNodeDomainValid(a,scene) || !PhysicsNodeDomainValid(b,scene) || x.owner!=y.owner
        || x.island_ids_valid!=y.island_ids_valid)return false;
    for(unsigned slot=0;slot<2;++slot)for(unsigned id=0;id<std::max(x.counts[slot],y.counts[slot]);++id) {
        const bool xd=id>=x.counts[slot] || DeletedPhysicsNode(x.nodes[slot][id],x.indices[slot][id]);
        const bool yd=id>=y.counts[slot] || DeletedPhysicsNode(y.nodes[slot][id],y.indices[slot][id]);
        if(xd!=yd)return false;
        if(!xd && x.island_ids_valid && x.island_ids[slot][id]!=y.island_ids[slot][id])return false;
        if(!xd && (x.nodes[slot][id][4]==std::byte{0x24} || y.nodes[slot][id][4]==std::byte{0x24})
            && (!x.activation_domain_clear[slot] || !y.activation_domain_clear[slot]))return false;
        if(!xd && x.nodes[slot][id]!=y.nodes[slot][id]) {
            auto normalized=x.nodes[slot][id];const auto& other=y.nodes[slot][id];
            const auto xf=std::to_integer<unsigned>(normalized[4]),yf=std::to_integer<unsigned>(other[4]);
            if((xf!=0x24 && (xf<5 || xf>7)) || (yf!=0x24 && (yf<5 || yf>7))
                || ReadAt<unsigned>(normalized.data(),0)!=0xffffffffu
                || ReadAt<unsigned>(normalized.data(),8)!=0xffffffc0u
                || ReadAt<unsigned>(normalized.data(),12)!=0xffffffc0u
                || ReadAt<unsigned short>(normalized.data(),6) || ReadAt<unsigned>(normalized.data(),16))return false;
            normalized[4]=other[4];if(normalized!=other)return false;
            bool isolated{};
            for(unsigned i=0;i<a.observed_actors[scene];++i)
                if(ReadAt<unsigned>(a.actors[scene][i].simulation_storage.data(),0xb0)==id*64
                    && a.actors[scene][i].simulation+0x60==ReadAt<std::uintptr_t>(normalized.data(),0x18))
                    isolated=IsolatedPhysicsKinematic(a,scene,i) && IsolatedPhysicsKinematic(b,scene,i)
                        && a.actors[scene][i].actor==b.actors[scene][i].actor
                        && a.actors[scene][i].simulation==b.actors[scene][i].simulation;
            if(!isolated)return false;
        }
    }
    return true;
}

bool PhysicsNodeAllocationEqual(const NodeDomain& a,const NodeDomain& b) noexcept {
    if(a.free_count>64 || b.free_count>64)return false;
    if(a.valid!=b.valid || a.owner!=b.owner || a.free_count!=b.free_count || a.next_id!=b.next_id || a.pending_count!=b.pending_count)return false;
    return std::equal(a.free_ids.begin(),a.free_ids.begin()+a.free_count,b.free_ids.begin());
}

bool PhysicsNodeLiveMatches(const NodeDomain& expected,const NodeDomain& bindings) noexcept {
    if(!expected.valid)return !bindings.valid;
    __try {
        const auto* owner=reinterpret_cast<void*>(bindings.owner);
        if(expected.owner!=bindings.owner || ReadAt<std::uintptr_t>(owner,0)!=bindings.free_storage
            || (ReadAt<unsigned>(owner,12)&0x7fffffff)!=bindings.free_capacity
            || ReadAt<unsigned>(owner,8)!=expected.free_count || ReadAt<unsigned>(owner,16)!=expected.next_id
            || ReadAt<unsigned>(owner,0x38)!=0 || expected.free_count>64
            || (expected.free_count && std::memcmp(reinterpret_cast<void*>(bindings.free_storage),expected.free_ids.data(),expected.free_count*4)))return false;
        for(unsigned slot=0;slot<2;++slot) {
            const auto* island=reinterpret_cast<void*>(bindings.owner+(slot?0x310:0xb0));
            const auto count=ReadAt<unsigned>(island,0x20);
            if(count!=bindings.counts[slot] || count>64 || ReadAt<unsigned>(island,0x30)!=count)return false;
            const auto nodes=ReadAt<std::uintptr_t>(island,0x18),indices=ReadAt<std::uintptr_t>(island,0x28);
            const auto ids=ReadAt<std::uintptr_t>(island,0xf0);
            if(expected.island_ids_valid && (!bindings.island_ids_valid || !ids
                || ReadAt<unsigned>(island,0xf8)!=bindings.island_id_counts[slot]))return false;
            if(expected.island_ids_valid && expected.activation_domain_clear[slot]) {
                if(ReadAt<unsigned>(island,0x160) || ReadAt<unsigned>(island,0x170) || ReadAt<unsigned>(island,0x1a8))return false;
                const auto bits=ReadAt<std::uintptr_t>(island,0x178);
                const auto words=ReadAt<unsigned>(island,0x180)&0x7fffffff;
                if(words>128 || (words&&!bits))return false;
                for(unsigned word=0;word<words;++word)if(ReadAt<unsigned>(reinterpret_cast<void*>(bits),word*4))return false;
            }
            for(unsigned id=0;id<count;++id) {
                std::array<std::byte,0x20> node{};std::memcpy(node.data(),reinterpret_cast<void*>(nodes+id*0x20),0x20);
                const auto index=ReadAt<unsigned>(reinterpret_cast<void*>(indices),id*4);
                if(id>=expected.counts[slot] || DeletedPhysicsNode(expected.nodes[slot][id],expected.indices[slot][id])) {
                    if(!DeletedPhysicsNode(node,index))return false;
                } else if(node!=expected.nodes[slot][id] || index!=expected.indices[slot][id]
                    || (expected.island_ids_valid && ReadAt<unsigned>(reinterpret_cast<void*>(ids),id*4)!=expected.island_ids[slot][id]))return false;
            }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool PhysicsNodeAllocationWritable(const NodeDomain& current,const NodeDomain& target) noexcept {
    return current.owner==target.owner && target.free_count<=current.free_capacity
        && Writable(reinterpret_cast<void*>(current.owner+8),4) && Writable(reinterpret_cast<void*>(current.owner+16),4)
        && (!target.free_count || Writable(reinterpret_cast<void*>(current.free_storage),target.free_count*4));
}

bool WritePhysicsNodeAllocation(const NodeDomain& target,const NodeDomain& bindings) noexcept {
    __try {
        if(target.free_count)std::memcpy(reinterpret_cast<void*>(bindings.free_storage),target.free_ids.data(),target.free_count*4);
        std::memcpy(reinterpret_cast<void*>(bindings.owner+16),&target.next_id,4);
        std::memcpy(reinterpret_cast<void*>(bindings.owner+8),&target.free_count,4);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
