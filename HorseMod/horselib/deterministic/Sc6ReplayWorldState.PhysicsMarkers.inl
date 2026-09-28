// Included inside Horse::Deterministic. All native access is confined to the
// host's quiescent physics boundary; no render work or allocator grows here.
struct Sc6ReplayPhysicsMarkers::State {
    using Graph=ReplayPhysicsMarkerGraph;
    enum class Phase { Prepared, Publishing, Published, Executing, Recovering, Recovered, Committed };
    enum class Slot { RetainedLive, Detached, Reserved, NativeLive, HandedOff, Returned };
    struct Domain {
        Graph a,b,scratch;
        std::array<std::uintptr_t,Graph::capacity> contacts{};unsigned contact_count{};
        std::uintptr_t scene{},nphase{},pool{};
        std::array<std::uintptr_t,Graph::capacity> copies{};
        std::array<Slot,Graph::capacity> original{},copy_state{};
        const SapImage* sap_a{};
        const SapImage* sap_b{};
        SapImage sap_current;
        const char* sap_check{};
        unsigned sap_row{0xffffffffu},sap_source{};std::uint64_t sap_expected{},sap_observed{};
        std::array<std::uintptr_t,SapImage::max_pairs> recovery_refs{};
        unsigned recovery_ref_count{};
        const char* live_check{};
        unsigned live_row{};std::uint64_t live_expected{},live_observed{};
        bool needed{};
    };
    std::array<Domain,2> domains{};
    const Boundary* target{};
    const Boundary* undo{};
    std::uintptr_t game{},module{};
    Phase phase{Phase::Prepared};
    unsigned failure_after{},completed_operations{};
    static constexpr std::size_t scratch_bytes=4096*sizeof(std::uintptr_t);

    template<class T> static T& at(std::uintptr_t p,unsigned offset=0) noexcept {
        return *reinterpret_cast<T*>(p+offset);
    }
    bool Signatures() const noexcept {
        const auto check=[](std::uintptr_t p,unsigned size,std::uint64_t expected) {
            std::uint64_t hash=14695981039346656037ull;
            for(unsigned i=0;i<size;++i)hash=(hash^at<unsigned char>(p,i))*1099511628211ull;
            return hash==expected;
        };
        return check(module+0x120030,160,0x8fbcc7e35a8c70b9ull)
            && check(module+0x135fb0,112,0x20a94d68c768cbe5ull)
            && check(module+0x11c410,112,0xf0de6ca7d0cd0903ull)
            && check(module+0x1220c0,80,0x7f4fef189240a1b4ull)
            && check(game+0x204cf60,704,0x9a921a6ea4013b30ull)
            && at<std::uintptr_t>(module+0x1aae40,8)==module+0x1143e0;
    }
    bool PoolContains(const Domain& d,std::uintptr_t slot) const noexcept {
        const auto slabs=at<std::uintptr_t>(d.pool,0x210);
        const auto count=at<unsigned>(d.pool,0x218),size=at<unsigned>(d.pool,0x228);
        if(!slot || !slabs || !count || count>64 || size<64 || size>4096*64)return false;
        unsigned hits{};
        for(unsigned i=0;i<count;++i) {
            const auto base=at<std::uintptr_t>(slabs,i*8);
            if(base && slot>=base && slot-base<size && !((slot-base)%64))++hits;
        }
        return hits==1;
    }
    bool PoolValid(const Domain& d,unsigned required_free=0) const noexcept {
        const auto slabs=at<std::uintptr_t>(d.pool,0x210);
        const auto count=at<unsigned>(d.pool,0x218),capacity=at<unsigned>(d.pool,0x21c)&0x7fffffffu;
        const auto slots=at<unsigned>(d.pool,0x220),size=at<unsigned>(d.pool,0x228),used=at<unsigned>(d.pool,0x224);
        if(!slabs || !count || count>64 || count>capacity || !slots || slots>4096
            || size!=slots*64 || count*slots>4096 || used>count*slots
            || !Writable(reinterpret_cast<void*>(d.pool+0x224),0x14))return false;
        for(unsigned i=0;i<count;++i)for(unsigned j=0;j<i;++j) {
            const auto a=at<std::uintptr_t>(slabs,i*8),b=at<std::uintptr_t>(slabs,j*8);
            if(!a || !b || (a>=b?a-b<size:b-a<size))return false;
        }
        std::array<std::uintptr_t,4096> free{};unsigned free_count{};
        for(auto slot=at<std::uintptr_t>(d.pool,0x230);slot;slot=at<std::uintptr_t>(slot)) {
            if(free_count>=count*slots || !PoolContains(d,slot) || !Writable(reinterpret_cast<void*>(slot),64))return false;
            for(unsigned i=0;i<free_count;++i)if(free[i]==slot)return false;
            for(unsigned i=0;i<d.b.count;++i)
                if(d.original[i]!=Slot::Returned && slot==d.b.entries[i].marker.address)return false;
            for(unsigned i=0;i<d.scratch.count;++i)if(slot==d.scratch.entries[i].marker.address)return false;
            for(unsigned i=0;i<d.a.count;++i)
                if(d.copy_state[i]==Slot::Reserved && slot==d.copies[i])return false;
            free[free_count++]=slot;
        }
        return free_count+used==count*slots && free_count>=required_free;
    }
    bool InputsLive(const Graph::Entry& e) const noexcept {
        const auto& m=e.marker;
        for(unsigned side=0;side<2;++side) {
            const auto shape=m.marker_elements[side];
            if(at<std::uintptr_t>(shape)!=module+0x1aae40
                || at<std::uintptr_t>(shape,0x10)!=e.owners[side]
                || at<std::uintptr_t>(shape,0x40)!=m.marker_shape_cores[side])return false;
            unsigned attributes{};std::array<unsigned,4> data{};
            reinterpret_cast<void(*)(void*,unsigned*,void*)>(module+0x1143e0)(reinterpret_cast<void*>(shape),&attributes,data.data());
            if(attributes!=m.marker_attributes[side] || data!=m.marker_filter_data[side])return false;
        }
        return true;
    }
    bool TargetInputsLive(const Domain& d) const noexcept {
        if(!d.sap_a)return d.a.TargetRetainedIn(d.b);
        // An overlap can disappear while both shape owners survive. The SAP
        // inventory independently covers their live IDs, scene membership,
        // core and consumed filter inputs; the old marker allocation is not
        // a lifetime lease. Construct always allocates a fresh native marker.
        for(unsigned i=0;i<d.a.count;++i) {
            const auto& e=d.a.entries[i];
            if(!InputsLive(e))return false;
            for(unsigned side=0;side<2;++side) {
                unsigned matches{};
                for(unsigned j=0;j<d.sap_current.extent;++j) {
                    const auto& v=d.sap_current.volumes[j];
                    if(v.shape!=e.marker.marker_elements[side])continue;
                    if(v.empty_aggregate || v.actor!=e.owners[side]
                        || v.core!=e.marker.marker_shape_cores[side]
                        || v.attributes!=e.marker.marker_attributes[side]
                        || v.filter_data!=e.marker.marker_filter_data[side])return false;
                    ++matches;
                }
                if(matches!=1 || at<std::uintptr_t>(e.owners[side],0x40)!=d.scene)return false;
            }
        }
        return true;
    }
    bool Bindings(const Domain& d) const noexcept {
        if(!(at<std::uintptr_t>(d.scene,0x1058)==d.nphase && d.pool==d.nphase+0xdf0
            && at<std::uintptr_t>(d.scene,0x1070)==game+0x204cf60
            && !at<std::uintptr_t>(d.scene,0x1078)))return false;
        const auto scene=static_cast<unsigned>(&d-domains.data());
        if(undo && scene<undo->notifications.size() && undo->notifications[scene].valid) {
            ReplayPhysicsNotifications current;
            return undo->notifications[scene].scene==d.scene
                && undo->notifications[scene].ClientsLive(PhysicsNotificationRead{})
                && ReadPhysicsNotificationsForScene(*undo,scene,d.scene,current);
        }
        return !at<unsigned>(d.scene,0x1080+0x34) && !at<unsigned>(d.scene,0x10b8+0x34);
    }
    bool ReadLive(Domain& d,bool commit=false) const noexcept {
        d.live_check=nullptr;d.live_row=0;d.live_expected=d.live_observed=0;
        const auto reject=[&](const char* check,unsigned row=0,std::uint64_t expected=0,std::uint64_t observed=0) {
            d.live_check=check;d.live_row=row;d.live_expected=expected;d.live_observed=observed;return false;
        };
        auto& g=d.scratch;g.count=at<unsigned>(d.scene,0x60);
        const auto data=at<std::uintptr_t>(d.scene,0x58);
        const auto cap=at<unsigned>(d.scene,0x64)&0x7fffffffu;
        const auto map=d.nphase+0x1558,entries=at<std::uintptr_t>(map,8);
        if(g.count>Graph::capacity || g.count>cap)return reject("marker_live_count",0,cap,g.count);
        if(at<unsigned>(d.scene,0x70))return reject("marker_live_active",0,0,at<unsigned>(d.scene,0x70));
        const auto map_count=at<unsigned>(map,0x34);
        if((!commit && map_count!=g.count) || map_count>Graph::capacity*2)
            return reject("marker_live_map_count",0,g.count,map_count);
        if(g.count && (!data || !entries))return reject("marker_live_storage",0,data,entries);
        if(!Bindings(d)) {
            if(at<std::uintptr_t>(d.scene,0x1058)!=d.nphase)return reject("marker_live_nphase",0,d.nphase,at<std::uintptr_t>(d.scene,0x1058));
            if(at<std::uintptr_t>(d.scene,0x1070)!=game+0x204cf60)return reject("marker_live_filter_shader",0,game+0x204cf60,at<std::uintptr_t>(d.scene,0x1070));
            if(at<std::uintptr_t>(d.scene,0x1078))return reject("marker_live_filter_callback",0,0,at<std::uintptr_t>(d.scene,0x1078));
            return reject("marker_live_notifications",0,at<unsigned>(d.scene,0x1080+0x34),at<unsigned>(d.scene,0x10b8+0x34));
        }
        d.contact_count=0;
        if(commit) {
            d.contact_count=at<unsigned>(d.scene,0x40);
            const auto list=at<std::uintptr_t>(d.scene,0x38);const auto capacity=at<unsigned>(d.scene,0x44)&0x7fffffffu;
            const auto active=at<unsigned>(d.scene,0x68);
            if(d.contact_count>Graph::capacity || d.contact_count>capacity || active>d.contact_count
                || (d.contact_count && !list))return reject("contact_live_storage",0,capacity,d.contact_count);
            for(unsigned i=0;i<d.contact_count;++i) {
                const auto entry=d.contacts[i]=at<std::uintptr_t>(list,i*8);
                if(!entry || at<std::uintptr_t>(entry)!=module+0x1ab540 || at<unsigned char>(entry,0x24)!=0)
                    return reject("contact_live_type",i,module+0x1ab540,entry?at<std::uintptr_t>(entry):0);
                const auto flags=at<unsigned char>(entry,0x25),dirty=at<unsigned char>(entry,0x26);
                if((flags&0xb)!=0xb || (flags&~0x5b) || bool(flags&0x40)!=(i<active)
                    || (dirty&~0x3f) || bool(flags&0x10)!=bool(dirty) || at<unsigned>(entry,0x38)!=0xffffffffu
                    || at<unsigned>(entry,0x18)!=i)return reject("contact_live_flags",i,i,at<unsigned>(entry,0x24));
                if(at<std::uintptr_t>(entry,8)==at<std::uintptr_t>(entry,16))return reject("contact_live_owners",i);
                for(unsigned side=0;side<2;++side) {
                    const auto owner=at<std::uintptr_t>(entry,8+side*8),shape=at<std::uintptr_t>(entry,0x28+side*8);
                    if(!owner || !shape || at<std::uintptr_t>(owner,0x40)!=d.scene
                        || at<std::uintptr_t>(shape)!=module+0x1aae40 || at<std::uintptr_t>(shape,0x10)!=owner
                        || !at<std::uintptr_t>(shape,0x40))return reject("contact_live_shape_owner",i,owner,shape);
                    const auto index=at<unsigned>(entry,0x1c+side*4),count=at<unsigned>(owner,0x34);
                    const auto entries=at<std::uintptr_t>(owner,0x28);
                    if(count>Graph::capacity || index>=count || !entries || at<std::uintptr_t>(entries,index*8)!=entry)
                        return reject("contact_live_actor_index",i,count,index);
                }
            }
        }
        if(commit && (map_count<g.count || map_count>g.count+d.contact_count
            || map_count>at<unsigned>(map,0x20) || (map_count && !entries)))
            return reject("marker_contact_map_count",0,g.count+d.contact_count,map_count);
        for(unsigned i=0;i<g.count;++i) {
            Graph::Marker m{};m.address=at<std::uintptr_t>(data,i*8);
            if(!PoolContains(d,m.address))return reject("marker_live_pool_slot",i,0,m.address);
            if(at<std::uintptr_t>(m.address)!=module+0x1ab300)return reject("marker_live_vtable",i,module+0x1ab300,at<std::uintptr_t>(m.address));
            std::memcpy(m.header.data(),reinterpret_cast<void*>(m.address),m.header.size());
            std::memcpy(m.marker_elements.data(),reinterpret_cast<void*>(m.address+0x28),16);
            m.marker_filter_pair=at<unsigned>(m.address,0x38);
            m.marker_scene_count=g.count;m.marker_map_count=map_count;m.marker_registered=m.marker_filter_valid=true;
            for(unsigned side=0;side<2;++side) {
                const auto shape=m.marker_elements[side],owner=at<std::uintptr_t>(m.address,8+side*8);
                if(!shape || !owner || at<std::uintptr_t>(shape)!=module+0x1aae40
                    || at<std::uintptr_t>(shape,0x10)!=owner || at<std::uintptr_t>(owner,0x40)!=d.scene)return reject("marker_live_shape_owner",i,owner,shape);
                m.marker_shape_cores[side]=at<std::uintptr_t>(shape,0x40);
                m.marker_actor_counts[side]=at<unsigned>(owner,0x34);
                const auto index=at<unsigned>(m.address,0x1c+side*4);
                const auto list=at<std::uintptr_t>(owner,0x28);
                if(!list || index>=m.marker_actor_counts[side])return reject("marker_live_actor_index",i,m.marker_actor_counts[side],index);
                if(at<std::uintptr_t>(list,index*8)!=m.address)return reject("marker_live_actor_entry",i,m.address,at<std::uintptr_t>(list,index*8));
                reinterpret_cast<void(*)(void*,unsigned*,void*)>(module+0x1143e0)(reinterpret_cast<void*>(shape),
                    &m.marker_attributes[side],m.marker_filter_data[side].data());
            }
            unsigned matches{};
            for(unsigned j=0;j<map_count;++j) {
                const auto a=at<std::uintptr_t>(entries,j*24),b=at<std::uintptr_t>(entries,j*24+8);
                if(a==(std::min)(m.marker_elements[0],m.marker_elements[1])
                    && b==(std::max)(m.marker_elements[0],m.marker_elements[1])) {
                    if(at<std::uintptr_t>(entries,j*24+16)!=m.address)return reject("marker_live_map_entry",i,m.address,at<std::uintptr_t>(entries,j*24+16));
                    ++matches;
                }
            }
            if(matches!=1)return reject("marker_live_map_matches",i,1,matches);
            if(!(commit?Graph::DecodeCommit(m,g.entries[i]):Graph::Decode(m,g.entries[i])))return reject("marker_live_decode",i,0xb,std::to_integer<unsigned>(m.header[0x25]));
            if(g.entries[i].indices[0]!=i)return reject("marker_live_scene_index",i,i,g.entries[i].indices[0]);
        }
        if(commit) {
            // This shared native map contains both markers and contacts. A
            // converted contact may have no map row; every present row must
            // resolve to exactly one independently validated C registration.
            std::array<bool,Graph::capacity*2> mapped{};
            for(unsigned i=0;i<map_count;++i) {
                const auto a=at<std::uintptr_t>(entries,i*24),b=at<std::uintptr_t>(entries,i*24+8);
                const auto entry=at<std::uintptr_t>(entries,i*24+16);
                unsigned row{};for(;row<g.count+d.contact_count;++row)
                    if((row<g.count?g.entries[row].marker.address:d.contacts[row-g.count])==entry)break;
                if(row==g.count+d.contact_count || mapped[row])return reject("marker_contact_map_foreign",i,0,entry);
                const auto x=at<std::uintptr_t>(entry,0x28),y=at<std::uintptr_t>(entry,0x30);
                if(a!=(std::min)(x,y) || b!=(std::max)(x,y))return reject("marker_contact_map_shapes",i,a,x);
                for(unsigned j=0;j<i;++j)if(at<std::uintptr_t>(entries,j*24)==a && at<std::uintptr_t>(entries,j*24+8)==b)
                    return reject("marker_contact_map_duplicate",i,a,b);
                mapped[row]=true;
            }
        }
        if(commit) {
            // Native 11B680/121A70 and 11B6C0/122F60 own this dense hash set.
            // Read only: pending C work belongs to the next native update.
            const auto set=d.nphase+0x60,dirty=at<std::uintptr_t>(set,8);
            const auto next=at<std::uintptr_t>(set,0x10),buckets=at<std::uintptr_t>(set,0x18);
            const auto n=at<unsigned>(set,0x34),capacity=at<unsigned>(set,0x20),size=at<unsigned>(set,0x24);
            if(n>g.count+d.contact_count || n>capacity || n!=at<unsigned>(set,0x2c) || capacity>4096 || size>8192
                || (size && (size&(size-1))) || (n && (!size || !dirty || !next || !buckets)))
                return reject("marker_dirty_storage",0,g.count,n);
            std::array<bool,Graph::capacity*2> seen{},members{};
            unsigned visited{};
            for(unsigned bucket=0;bucket<size;++bucket) {
                auto index=at<unsigned>(buckets,bucket*4);
                while(index!=0xffffffffu) {
                    if(index>=n || seen[index])return reject("marker_dirty_chain",bucket,n,index);
                    seen[index]=true;++visited;
                    const auto entry=at<std::uintptr_t>(dirty,index*8);
                    std::uint64_t h=(~(entry<<32))+entry;h^=h>>22;h+=~(h<<13);
                    h^=h>>8;h*=9;h^=h>>15;h+=~(h<<27);h^=h>>31;
                    if((unsigned(h)&(size-1))!=bucket)return reject("marker_dirty_bucket",index,bucket,unsigned(h)&(size-1));
                    unsigned row{};for(;row<g.count+d.contact_count;++row)
                        if((row<g.count?g.entries[row].marker.address:d.contacts[row-g.count])==entry)break;
                    if(row==g.count+d.contact_count || members[row])return reject("marker_dirty_foreign",index,0,entry);
                    members[row]=true;index=at<unsigned>(next,index*4);
                }
            }
            if(visited!=n)return reject("marker_dirty_count",0,n,visited);
            for(unsigned i=0;i<g.count+d.contact_count;++i) {
                const auto entry=i<g.count?g.entries[i].marker.address:d.contacts[i-g.count];
                if(members[i]!=bool(at<unsigned char>(entry,0x25)&0x10))
                    return reject("marker_dirty_membership",i,members[i],at<unsigned char>(entry,0x25));
            }
        }
        if(commit && g.Complete(true)) {
            // Every actor-list member must be one of the independently read
            // registered C markers/contacts. No unknown or retained B links.
            for(unsigned i=0;i<g.count+d.contact_count;++i)for(unsigned side=0;side<2;++side) {
                const auto entry=i<g.count?g.entries[i].marker.address:d.contacts[i-g.count];
                const auto owner=at<std::uintptr_t>(entry,8+side*8),list=at<std::uintptr_t>(owner,0x28);
                const auto count=at<unsigned>(owner,0x34);
                for(unsigned j=0;j<count;++j) {
                    const auto member=at<std::uintptr_t>(list,j*8);
                    bool found{};
                    for(unsigned k=0;k<g.count;++k)found|=g.entries[k].marker.address==member;
                    for(unsigned k=0;k<d.contact_count;++k)found|=d.contacts[k]==member;
                    if(!found)return reject("contact_live_actor_foreign",j,owner,member);
                    const auto side=at<std::uintptr_t>(member,8)==owner?0u:at<std::uintptr_t>(member,16)==owner?1u:2u;
                    if(side==2 || at<unsigned>(member,0x1c+side*4)!=j)
                        return reject("contact_live_actor_inverse",j,owner,member);
                }
            }
            return true;
        }
        if(!commit && g.Complete())return true;
        // Failure-only witness: historical marker completeness assumes every
        // actor interaction belongs to this graph. Identify the first native
        // consumer outside it before changing commit admission.
        for(unsigned i=0;i<g.count;++i)for(unsigned side=0;side<2;++side) {
            const auto actor=g.entries[i].owners[side],list=at<std::uintptr_t>(actor,0x28);
            const auto count=g.entries[i].marker.marker_actor_counts[side];
            if(count>Graph::capacity)return reject("marker_live_actor_capacity",i,Graph::capacity,count);
            for(unsigned j=0;j<count;++j) {
                const auto interaction=at<std::uintptr_t>(list,j*8);
                bool found{};for(unsigned k=0;k<g.count;++k)found|=g.entries[k].marker.address==interaction;
                if(!found)return reject("marker_live_extra_interaction",j,interaction,
                    std::uint64_t(at<unsigned>(interaction,0x24)) | (std::uint64_t(count)<<32));
            }
        }
        return reject("marker_live_complete",0,g.count,0);
    }
    bool Storage(const Domain& d,unsigned maximum) const noexcept {
        const auto map=d.nphase+0x1558;
        const auto capacity=at<unsigned>(map,0x20),buckets=at<unsigned>(map,0x24);
        if(maximum>(at<unsigned>(d.scene,0x64)&0x7fffffffu) || maximum>capacity || !buckets
            || capacity>4096 || buckets>8192
            || !Writable(reinterpret_cast<void*>(d.scene+0x58),0x20)
            || !Writable(reinterpret_cast<void*>(at<std::uintptr_t>(d.scene,0x58)),maximum*8)
            || !Writable(reinterpret_cast<void*>(map),0x50)
            || !Writable(reinterpret_cast<void*>(at<std::uintptr_t>(map,8)),capacity*24)
            || !Writable(reinterpret_cast<void*>(at<std::uintptr_t>(map,0x10)),capacity*4)
            || !Writable(reinterpret_cast<void*>(at<std::uintptr_t>(map,0x18)),buckets*4))return false;
        for(const auto* g:{&d.a,&d.b,&d.scratch})for(unsigned i=0;i<g->count;++i)for(unsigned side=0;side<2;++side) {
            const auto owner=g->entries[i].owners[side],list=at<std::uintptr_t>(owner,0x28);
            const auto cap=at<unsigned>(owner,0x30),count=g->entries[i].marker.marker_actor_counts[side];
            if(cap<count || cap>256 || !list || !Writable(reinterpret_cast<void*>(owner+8),0x30)
                || !Writable(reinterpret_cast<void*>(list),cap*8))return false;
            if(cap==4?list!=owner+8:(cap<8 || (cap&(cap-1)) || (list>=owner && list<owner+0xc8)))return false;
        }
        for(const auto* g:{&d.b,&d.scratch})for(unsigned i=0;i<g->count;++i)
            if(!Writable(reinterpret_cast<void*>(g->entries[i].marker.address),64))return false;
        return true;
    }
    bool Matches(const Domain& d,const Graph& desired,bool copies) const noexcept {
        if(d.scratch.count!=desired.count)return false;
        for(unsigned i=0;i<desired.count;++i) {
            const auto& a=desired.entries[i];const auto& b=d.scratch.entries[i];
            if(b.marker.address!=(copies?d.copies[i]:a.marker.address)
                || a.owners!=b.owners || a.indices!=b.indices || !Graph::InputsEqual(a.marker,b.marker))return false;
        }
        return true;
    }
    bool SapGraph(const SapImage& image,const Graph& graph,const Domain& d,bool copies) const noexcept {
        std::array<bool,Graph::capacity> seen{};
        for(unsigned i=0;i<image.pair_count;++i) {
            const auto& pair=image.pairs[i];if(!pair.interaction)continue;
            bool found{};
            for(unsigned j=0;j<graph.count;++j) {
                const auto& e=graph.entries[j];
                if(pair.interaction!=(copies?d.copies[j]:e.marker.address))continue;
                if(seen[j])return false;
                const auto a=image.volumes[pair.handles[0]].shape,b=image.volumes[pair.handles[1]].shape;
                if(!((a==e.marker.marker_elements[0] && b==e.marker.marker_elements[1])
                    || (a==e.marker.marker_elements[1] && b==e.marker.marker_elements[0])))return false;
                seen[j]=found=true;break;
            }
            if(!found)return false;
        }
        for(unsigned i=0;i<graph.count;++i)if(!seen[i])return false;
        return true;
    }
    bool SapCommitReferences(const Domain& d,Diagnostic& failure) const noexcept {
        if(!d.sap_a)return true;
        // Native conversion leaves dormant SAP cache entries naming freed
        // marker slots. E434F/EFD8E resolve overlap shapes through11AFE0 and
        // overwrite the consumed overlap pointer. Do not dereference the cache
        // as a current interaction or require contacts to have a SAP pair.
        // Retirement still excludes every reference to retained B, and accepts
        // only verified C registrations or slots in the validated free pool.
        for(unsigned i=0;i<d.sap_current.pair_count;++i) {
            const auto cached=d.sap_current.pairs[i].interaction;if(!cached)continue;
            for(unsigned j=0;j<d.b.count;++j)
                if(d.original[j]!=Slot::Returned && cached==d.b.entries[j].marker.address) {
                    failure={"sap_commit_retains_B",cached,i};return false;
                }
            bool known{};
            for(unsigned j=0;j<d.scratch.count;++j)known|=d.scratch.entries[j].marker.address==cached;
            for(unsigned j=0;j<d.contact_count;++j)known|=d.contacts[j]==cached;
            if(!known && PoolContains(d,cached)) {
                unsigned visited{};
                for(auto slot=at<std::uintptr_t>(d.pool,0x230);slot && visited++<4096;slot=at<std::uintptr_t>(slot))
                    if(slot==cached){known=true;break;}
            }
            if(!known){failure={"sap_commit_foreign_cache",cached,i};return false;}
        }
        return true;
    }
    bool SapLive(Domain& d) const noexcept {
        if(!d.sap_a)return true;
        if(!ReadPhysicsSap(module,d.scene,d.sap_current,d.sap_b->native_scene,&d.sap_check))return false;
        d.sap_check="sap_changed_input_owners";
        d.sap_source=0;
        if(!d.sap_a->SameOwners(d.sap_current,&d.sap_check,&d.sap_row,&d.sap_expected,&d.sap_observed))return false;
        d.sap_source=1;
        if(!d.sap_b->SameOwners(d.sap_current,&d.sap_check,&d.sap_row,&d.sap_expected,&d.sap_observed))return false;
        d.sap_check="sap_restore_storage";
        return PhysicsSapStorage(*d.sap_a,d.sap_current) && PhysicsSapStorage(*d.sap_b,d.sap_current);
    }
    bool SapCommit(Domain& d) const noexcept {
        if(!d.sap_a)return true;
        if(!ReadPhysicsSap(module,d.scene,d.sap_current,d.sap_b->native_scene,&d.sap_check))return false;
        // Commit never writes historical SAP storage. Native execution may
        // create/destroy shape and rigid IDs; validate C's complete native graph
        // under the original scene domain instead of requiring A/B membership.
        const auto& c=d.sap_current;
        for(const auto* old:{d.sap_a,d.sap_b}) {
            d.sap_check="sap_commit_domain";
            if(old->scene!=c.scene || old->native_scene!=c.native_scene || old->manager!=c.manager
                || old->broadphase!=c.broadphase || old->bounds_owner!=c.bounds_owner
                || old->distance_owner!=c.distance_owner || old->anchor_owner!=c.anchor_owner
                || old->anchor_core!=c.anchor_core || old->anchor_id!=c.anchor_id)return false;
            for(unsigned i=0;i<c.ids.size();++i)if(old->ids[i].owner!=c.ids[i].owner)return false;
            // Constraint lifetime is outside this shape/rigid participant.
            d.sap_check="sap_commit_constraints";
            if(old->ids[0].next!=c.ids[0].next || old->ids[0].count!=c.ids[0].count)return false;
            for(unsigned i=0;i<c.ids[0].count;++i)if(old->ids[0].free[i]!=c.ids[0].free[i])return false;
        }
        return true;
    }
    bool SapRecoverable(const Domain& d) const noexcept {
        if(!d.sap_a)return true;
        if(phase==Phase::Publishing)
            return SapGraph(d.sap_current,d.b,d,false) || SapGraph(d.sap_current,d.a,d,true);
        if(phase!=Phase::Recovering)return SapGraph(d.sap_current,d.scratch,d,false);
        // A retry may observe cached pointers to C slots which this undo
        // already retired. Only the explicitly journaled references or B's
        // retained slots are allowed; a foreign pointer remains a rejection.
        for(unsigned i=0;i<d.sap_current.pair_count;++i) {
            const auto pointer=d.sap_current.pairs[i].interaction;if(!pointer)continue;
            bool known{};
            for(unsigned j=0;j<d.recovery_ref_count;++j)known|=pointer==d.recovery_refs[j];
            for(unsigned j=0;j<d.b.count;++j)known|=pointer==d.b.entries[j].marker.address;
            if(!known)return false;
        }
        return true;
    }
    bool SapPublish(Domain& d,bool undo) const noexcept {
        if(!d.sap_a)return true;
        const auto& graph=undo?d.b:d.a;const auto& desired=undo?*d.sap_b:*d.sap_a;
        if(!WritePhysicsSap(desired,d.sap_current,[&](std::uintptr_t original) {
            for(unsigned i=0;i<graph.count;++i)if(graph.entries[i].marker.address==original)
                return undo?original:d.copies[i];
            return std::uintptr_t{};
        }) || !ReadPhysicsSap(module,d.scene,d.sap_current,d.sap_b->native_scene))return false;
        if(!SapGraph(d.sap_current,graph,d,!undo))return false;
        // Compare all captured continuation values after canonicalizing only
        // the registered interaction relocation; no expected observer input.
        for(unsigned i=0;i<desired.pair_count;++i)d.sap_current.pairs[i].interaction=desired.pairs[i].interaction;
        return d.sap_current.SameContinuation(desired);
    }
    void FreeRaw(Domain& d,std::uintptr_t slot) noexcept {
        // Tail of native1220C0. The destructor already ran exactly once.
        --at<unsigned>(d.pool,0x224);
        at<std::uintptr_t>(slot)=at<std::uintptr_t>(d.pool,0x230);
        at<std::uintptr_t>(d.pool,0x230)=slot;
    }
    void Construct(std::uintptr_t slot,const Graph::Entry& e,const Domain& d) const noexcept {
        reinterpret_cast<void*(*)(void*,std::uintptr_t,std::uintptr_t,bool)>(module+0x120030)(
            reinterpret_cast<void*>(slot),e.marker.marker_elements[0],e.marker.marker_elements[1],false);
        reinterpret_cast<void(*)(std::uintptr_t,std::uintptr_t)>(module+0x11c410)(d.nphase,slot);
    }
    void Detach(std::uintptr_t slot) const noexcept {
        reinterpret_cast<void(*)(void*)>(module+0x135fb0)(reinterpret_cast<void*>(slot));
    }
    void Order(Domain& d,const Graph& g,bool copies) const noexcept {
        const auto list=at<std::uintptr_t>(d.scene,0x58);
        for(unsigned i=0;i<g.count;++i) {
            const auto slot=copies?d.copies[i]:g.entries[i].marker.address;
            at<std::uintptr_t>(list,i*8)=slot;at<unsigned>(slot,0x18)=i;
            for(unsigned side=0;side<2;++side) {
                const auto index=g.entries[i].indices[side+1];
                const auto owner=g.entries[i].owners[side];
                at<std::uintptr_t>(at<std::uintptr_t>(owner,0x28),index*8)=slot;
                at<unsigned>(slot,0x1c+side*4)=index;
            }
        }
    }
};

Sc6ReplayPhysicsMarkers::Sc6ReplayPhysicsMarkers()=default;
Sc6ReplayPhysicsMarkers::~Sc6ReplayPhysicsMarkers()=default;
std::size_t Sc6ReplayPhysicsMarkers::owned_bytes() const noexcept {
    return state_?sizeof(State)+State::scratch_bytes+2*ReplayPhysicsMarkerGraph::capacity*64:0;
}
bool Sc6ReplayPhysicsMarkers::released() const noexcept {
    return !state_ || state_->phase==State::Phase::Prepared || state_->phase==State::Phase::Recovered || state_->phase==State::Phase::Committed;
}

Status Sc6ReplayPhysicsMarkers::Prepare(std::uintptr_t game_base,const Boundary& a,const Boundary& b,std::size_t budget) noexcept {
    if(state_)return Status::failure(FailureCode::IllegalTransition);
    if(!a.valid_counts() || !b.valid_counts() || a.module!=b.module || a.scenes!=b.scenes
        || a.observed_actors!=b.observed_actors)return Status::failure(FailureCode::GenerationMismatch);
    bool changed{};
    for(unsigned scene=0;scene<2;++scene) {
        for(const auto* image:{&a.broadphases[scene],&b.broadphases[scene]})
            if(image->scene && !image->valid) {
                diagnostic_={SapImage::AdmissionName(image->admission_step),image->scene};
                return Status::failure(FailureCode::UnsupportedContent);
            }
        changed|=a.broadphases[scene]!=b.broadphases[scene];
        for(unsigned i=0;i<a.observed_actors[scene];++i)
            changed|=a.actors[scene][i].interaction_count!=b.actors[scene][i].interaction_count;
    }
    if(!changed)return Status::success();
    if(sizeof(State)+State::scratch_bytes+2*ReplayPhysicsMarkerGraph::capacity*64>budget)
        return Status::failure(FailureCode::CapacityExceeded);
    state_.reset(new(std::nothrow) State);
    if(!state_)return Status::failure(FailureCode::CapacityExceeded);
    auto& s=*state_;s.game=game_base;s.module=a.module;s.target=&a;s.undo=&b;
    bool valid=true;
    __try {
        diagnostic_={"marker_native_signatures",a.module};
        valid=s.Signatures();
        for(unsigned scene=0;valid && scene<2;++scene) {
            auto& d=s.domains[scene];
            d.needed=a.broadphases[scene]!=b.broadphases[scene];
            for(unsigned i=0;i<a.observed_actors[scene];++i)
                d.needed|=a.actors[scene][i].interaction_count!=b.actors[scene][i].interaction_count;
            if(!d.needed)continue;
            diagnostic_={"marker_notification_ownership",scene};
            valid=PhysicsNotificationPairValid(a,b,scene)
                && (!b.notifications[scene].valid || PhysicsNotificationsLiveMatch(b,scene));
            if(!valid)break;
            ReplayPhysicsMarkerGraph::ReadFailure failure{};
            diagnostic_={"marker_captured_graph_A",scene};
            valid=d.a.Read(a,scene,&failure);
            if(!valid)diagnostic_={failure.check,failure.actor,failure.interaction,0,failure.expected,failure.observed};
            if(valid) {
                valid=d.b.Read(b,scene,&failure);
                if(!valid)diagnostic_={failure.check,failure.actor,failure.interaction,1,failure.expected,failure.observed};
            }
            if(!valid)break;
            // Empty A constructs no marker. The existing transaction still
            // detaches/retains complete B and publishes the audited empty SAP
            // image; native C may create markers before undo or commit. An
            // empty graph alone cannot establish its scene/shape ownership.
            if(!d.a.count && (!a.broadphases[scene].valid || !b.broadphases[scene].valid
                || !a.broadphases[scene].scene || a.broadphases[scene].scene!=b.broadphases[scene].scene
                || !a.broadphases[scene].native_scene
                || a.broadphases[scene].native_scene!=b.broadphases[scene].native_scene)) {
                diagnostic_={"marker_empty_target_scene",scene};valid=false;break;
            }
            if(d.b.count) d.scene=State::at<std::uintptr_t>(d.b.entries[0].owners[0],0x40);
            else {
                // Ending the final native overlap does not destroy ScScene,
                // its shapes or the pool. There is no B marker from which to
                // infer the scene. Use the already captured full SAP owner;
                // SapLive/TargetInputsLive below independently recheck native
                // scene membership, IDs, shape/core/filter bindings and pairs.
                // Empty graphs without this retained owner remain unsupported.
                diagnostic_={"marker_empty_graph_scene",scene,0xffffffffu,0,d.a.count,d.b.count};
                valid=a.broadphases[scene].valid && b.broadphases[scene].valid
                    && b.broadphases[scene].scene && a.broadphases[scene].scene==b.broadphases[scene].scene
                    && a.broadphases[scene].native_scene==b.broadphases[scene].native_scene;
                if(!valid)break;
                d.scene=b.broadphases[scene].scene;
                if(!State::at<std::uintptr_t>(d.scene,0x728)) {valid=false;break;}
            }
            d.nphase=State::at<std::uintptr_t>(d.scene,0x1058);d.pool=d.nphase+0xdf0;
            // Native11ACF0/100DE0 publish interaction pointers through SAP.
            // A missing/unqualified image still rejects before any write.
            if(State::at<std::uintptr_t>(d.scene,0x728)) {
                diagnostic_={"marker_broadphase_ownership_unproven",State::at<std::uintptr_t>(d.scene,0x728)};
                d.sap_a=&a.broadphases[scene];d.sap_b=&b.broadphases[scene];
                valid=d.sap_a->valid && d.sap_b->valid && s.SapLive(d)
                    && d.sap_current==*d.sap_b && s.SapGraph(*d.sap_a,d.a,d,false) && s.SapGraph(*d.sap_b,d.b,d,false);
                if(!valid && d.sap_check)diagnostic_={d.sap_check,d.scene,d.sap_row,d.sap_source,d.sap_expected,d.sap_observed};
                if(!valid)break;
            }
            diagnostic_={"marker_target_shape_ownership",d.scene};
            valid=s.TargetInputsLive(d);
            if(!valid)break;
            const auto& filter=b.node_domains[scene];
            diagnostic_={"marker_filter_binding",d.scene};
            valid=filter.filter_shader==game_base+0x204cf60 && !filter.filter_callback
                && a.node_domains[scene].filter_shader==filter.filter_shader && !a.node_domains[scene].filter_callback;
            if(valid) {
                diagnostic_={"marker_live_graph",d.scene};valid=s.ReadLive(d);
                if(!valid && d.live_check)diagnostic_={d.live_check,d.scene,d.live_row,0,d.live_expected,d.live_observed};
                if(valid) {diagnostic_={"marker_live_B_mismatch",d.scene,0xffffffffu,0,d.b.count,d.scratch.count};valid=s.Matches(d,d.b,false);}
            }
            if(valid) {diagnostic_={"marker_pool_ownership",d.pool};valid=s.PoolValid(d,d.a.count);}
            if(valid) {diagnostic_={"marker_capacity_and_storage",d.nphase};valid=s.Storage(d,(std::max)(d.a.count,d.b.count));}
            if(valid)diagnostic_={"marker_retained_inputs",d.scene};
            for(unsigned i=0;valid && i<d.b.count;++i)valid=s.InputsLive(d.b.entries[i]);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {valid=false;}
    if(!valid) {state_.reset();return Status::failure(FailureCode::RestorePreflightFailed);}
    diagnostic_={};
    return Status::success();
}

bool Sc6ReplayPhysicsMarkers::NormalizePreflight(const Boundary& a,const Boundary& b,unsigned scene,unsigned actor,
    Boundary::ActorObservation& normalized) const noexcept {
    if(!state_ || state_->phase!=State::Phase::Prepared || state_->target!=&a || state_->undo!=&b
        || scene>=2 || actor>=a.observed_actors[scene] || !state_->domains[scene].needed)return false;
    const auto& row=a.actors[scene][actor];const auto& current=b.actors[scene][actor];
    bool member{};
    const auto& domain=state_->domains[scene];
    // The transaction owns native registration changes on both graphs. An
    // actor can have an empty B list yet receive a verified A pair. Its shape,
    // scene and filter bindings were independently checked during Prepare.
    for(const auto* graph:{&domain.a,&domain.b})
        for(unsigned i=0;i<graph->count;++i)for(const auto owner:graph->entries[i].owners)member|=row.simulation==owner;
    // An isolated witness cannot describe a captured occupied list. Checking
    // before normalization prevents empty B from hiding inconsistent A input.
    if(!member || row.simulation!=current.simulation
        || (row.isolated_kinematic && row.interaction_count)
        || (current.isolated_kinematic && current.interaction_count))return false;
    normalized.interaction_count=current.interaction_count;
    normalized.interactions=current.interactions;
    // Native actor-list publication owns only slots/backing/count. Shape
    // links at +38 and scene/solver fields remain independently compared.
    std::memcpy(normalized.simulation_storage.data()+8,current.simulation_storage.data()+8,0x30);
    return true;
}

Status Sc6ReplayPhysicsMarkers::Publish() noexcept {
    if(!state_)return Status::success();auto& s=*state_;
    if(s.phase!=State::Phase::Prepared)return Status::failure(FailureCode::IllegalTransition);
    __try {
        // Validate every domain before changing any pool or registration.
        if(!s.Signatures())return Status::failure(FailureCode::IdentityMismatch);
        for(auto& d:s.domains)if(d.needed && (!s.ReadLive(d) || !s.Matches(d,d.b,false)
            || !s.PoolValid(d,d.a.count) || !s.Storage(d,(std::max)(d.a.count,d.b.count))
            || !s.SapLive(d) || (d.sap_b && d.sap_current!=*d.sap_b) || !s.TargetInputsLive(d)))
            return Status::failure(FailureCode::GenerationMismatch);
        s.phase=State::Phase::Publishing;
        for(auto& d:s.domains)if(d.needed) {
            for(unsigned i=0;i<d.a.count;++i) {
                const auto slot=State::at<std::uintptr_t>(d.pool,0x230);
                State::at<std::uintptr_t>(d.pool,0x230)=State::at<std::uintptr_t>(slot);
                ++State::at<unsigned>(d.pool,0x224);
                d.copies[i]=slot;d.copy_state[i]=State::Slot::Reserved;
            }
            for(unsigned i=d.b.count;i--;) {
                s.Detach(d.b.entries[i].marker.address);d.original[i]=State::Slot::Detached;
                if(++s.completed_operations==s.failure_after)return Status::failure(FailureCode::RestoreVerificationFailed);
            }
            for(unsigned i=0;i<d.a.count;++i) {
                s.Construct(d.copies[i],d.a.entries[i],d);d.copy_state[i]=State::Slot::NativeLive;
                if(++s.completed_operations==s.failure_after)return Status::failure(FailureCode::RestoreVerificationFailed);
            }
            s.Order(d,d.a,true);
            if(!s.SapPublish(d,false) || !s.ReadLive(d) || !s.Matches(d,d.a,true) || !s.PoolValid(d))
                return Status::failure(FailureCode::RestoreVerificationFailed);
            if(++s.completed_operations==s.failure_after)return Status::failure(FailureCode::RestoreVerificationFailed);
        }
        s.phase=State::Phase::Published;return Status::success();
    } __except(EXCEPTION_EXECUTE_HANDLER) {return Status::failure(FailureCode::RestoreVerificationFailed);}
}

bool Sc6ReplayPhysicsMarkers::ArmPublicationFailure(unsigned completed_operations) noexcept {
    if(!state_ || state_->phase!=State::Phase::Prepared || !completed_operations || state_->failure_after)return false;
    unsigned total{};for(const auto& d:state_->domains)if(d.needed)total+=d.a.count+d.b.count+1;
    if(completed_operations>total)return false;
    state_->failure_after=completed_operations;return true;
}

Status Sc6ReplayPhysicsMarkers::BeginExecution() noexcept {
    if(!state_)return Status::success();auto& s=*state_;
    if(s.phase==State::Phase::Executing)return Status::success();
    if(s.phase!=State::Phase::Published)return Status::failure(FailureCode::IllegalTransition);
    __try {
        for(auto& d:s.domains)if(d.needed && (!s.ReadLive(d) || !s.Matches(d,d.a,true) || !s.PoolValid(d)
            || !s.SapLive(d) || (d.sap_a && !s.SapGraph(d.sap_current,d.a,d,true))))
            return Status::failure(FailureCode::GenerationMismatch);
        for(auto& d:s.domains)if(d.needed)for(unsigned i=0;i<d.a.count;++i)d.copy_state[i]=State::Slot::HandedOff;
        s.phase=State::Phase::Executing;return Status::success();
    } __except(EXCEPTION_EXECUTE_HANDLER) {return Status::failure(FailureCode::GenerationMismatch);}
}

Status Sc6ReplayPhysicsMarkers::Undo() noexcept {
    if(!state_)return Status::success();auto& s=*state_;
    if(s.phase==State::Phase::Prepared || s.phase==State::Phase::Recovered)return Status::success();
    if(s.phase==State::Phase::Committed)return Status::failure(FailureCode::IllegalTransition);
    __try {
        if(!s.Signatures())return Status::failure(FailureCode::IdentityMismatch);
        // Complete current C is inspected before any C retirement or B write.
        for(auto& d:s.domains)if(d.needed) {
            if(!s.ReadLive(d) || !s.PoolValid(d) || !s.Storage(d,(std::max)(d.scratch.count,d.b.count))
                || !s.SapLive(d) || !s.SapRecoverable(d))
                return Status::failure(FailureCode::UndoFailed);
            for(unsigned i=0;i<d.b.count;++i)if(!s.InputsLive(d.b.entries[i]))return Status::failure(FailureCode::UndoFailed);
        }
        if(s.phase!=State::Phase::Recovering)for(auto& d:s.domains)if(d.needed && d.sap_a) {
            d.recovery_ref_count=d.sap_current.pair_count;
            for(unsigned i=0;i<d.recovery_ref_count;++i)d.recovery_refs[i]=d.sap_current.pairs[i].interaction;
        }
        s.phase=State::Phase::Recovering;
        for(auto& d:s.domains)if(d.needed) {
            for(unsigned i=d.scratch.count;i--;) {
                const auto slot=d.scratch.entries[i].marker.address;bool retained{};
                for(unsigned j=0;j<d.b.count;++j)retained|=slot==d.b.entries[j].marker.address && d.original[j]==State::Slot::RetainedLive;
                if(retained)continue;
                s.Detach(slot);s.FreeRaw(d,slot);
                for(unsigned j=0;j<d.a.count;++j)if(d.copies[j]==slot)d.copy_state[j]=State::Slot::Returned;
            }
            for(unsigned i=0;i<d.a.count;++i)if(d.copy_state[i]==State::Slot::Reserved) {
                s.FreeRaw(d,d.copies[i]);d.copy_state[i]=State::Slot::Returned;
            }
            for(unsigned i=0;i<d.b.count;++i)if(d.original[i]==State::Slot::Detached) {
                s.Construct(d.b.entries[i].marker.address,d.b.entries[i],d);d.original[i]=State::Slot::RetainedLive;
            }
            s.Order(d,d.b,false);
            if(!s.SapPublish(d,true) || !s.ReadLive(d) || !s.Matches(d,d.b,false) || !s.PoolValid(d))return Status::failure(FailureCode::UndoFailed);
        }
        s.phase=State::Phase::Recovered;return Status::success();
    } __except(EXCEPTION_EXECUTE_HANDLER) {return Status::failure(FailureCode::UndoFailed);}
}

Status Sc6ReplayPhysicsMarkers::ValidateCommit() noexcept {
    diagnostic_={};
    if(!state_)return Status::success();auto& s=*state_;
    if(s.phase==State::Phase::Committed)return Status::success();
    if(s.phase!=State::Phase::Published && s.phase!=State::Phase::Executing)
        return Status::failure(FailureCode::IllegalTransition);
    __try {
        for(auto& d:s.domains)if(d.needed) {
            if(!s.ReadLive(d,true)) {
                diagnostic_={d.live_check?d.live_check:"marker_live",d.scene,d.live_row,0,d.live_expected,d.live_observed};
                return Status::failure(FailureCode::GenerationMismatch);
            }
            if(!s.PoolValid(d)) {
                diagnostic_={"marker_pool",d.pool};return Status::failure(FailureCode::GenerationMismatch);
            }
            if(!s.SapCommit(d)) {
                diagnostic_={d.sap_check?d.sap_check:"marker_sap",d.scene,d.sap_row,d.sap_source,d.sap_expected,d.sap_observed};
                return Status::failure(FailureCode::GenerationMismatch);
            }
            if(!s.SapCommitReferences(d,diagnostic_)) {return Status::failure(FailureCode::GenerationMismatch);
            }
            for(unsigned i=0;i<d.b.count;++i) {
                if(d.original[i]!=State::Slot::Returned && d.original[i]!=State::Slot::Detached) {
                    diagnostic_={"marker_B_ownership",d.b.entries[i].marker.address,i};
                    return Status::failure(FailureCode::IllegalTransition);
                }
                if(d.original[i]!=State::Slot::Returned)for(unsigned j=0;j<d.contact_count;++j)
                    if(d.b.entries[i].marker.address==d.contacts[j]) {
                        diagnostic_={"contact_C_aliases_B",d.contacts[j],j};return Status::failure(FailureCode::GenerationMismatch);
                    }
                if(d.original[i]!=State::Slot::Returned)
                    for(unsigned j=0;j<d.scratch.count;++j)
                        if(d.b.entries[i].marker.address==d.scratch.entries[j].marker.address) {
                            diagnostic_={"marker_C_aliases_B",d.b.entries[i].marker.address,j};
                            return Status::failure(FailureCode::GenerationMismatch);
                        }
            }
        }
        return Status::success();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        diagnostic_={"marker_commit_read_fault"};return Status::failure(FailureCode::RestoreVerificationFailed);
    }
}

Status Sc6ReplayPhysicsMarkers::Commit() noexcept {
    if(!state_)return Status::success();auto& s=*state_;
    if(s.phase==State::Phase::Committed)return Status::success();
    if(s.phase!=State::Phase::Published && s.phase!=State::Phase::Executing)return Status::failure(FailureCode::IllegalTransition);
    __try {
        const auto valid=ValidateCommit();if(!valid.ok())return valid;
        for(auto& d:s.domains)if(d.needed)for(unsigned i=0;i<d.b.count;++i) {
            if(d.original[i]==State::Slot::Returned)continue;
            if(d.original[i]!=State::Slot::Detached)return Status::failure(FailureCode::IllegalTransition);
            s.FreeRaw(d,d.b.entries[i].marker.address);d.original[i]=State::Slot::Returned;
        }
        s.phase=State::Phase::Committed;return Status::success();
    } __except(EXCEPTION_EXECUTE_HANDLER) {return Status::failure(FailureCode::RestoreVerificationFailed);}
}
