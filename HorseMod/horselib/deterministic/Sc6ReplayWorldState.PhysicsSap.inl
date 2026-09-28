// Included in WorldState's native-access namespace. The caller owns a completed
// application boundary and the scene writer lock. No native allocation occurs.
using SapImage=ReplayPhysicsSapImage;
unsigned PhysicsSapHash(unsigned a,unsigned b) noexcept;
bool ReadPhysicsSap(std::uintptr_t module,std::uintptr_t scene,SapImage& out,std::uintptr_t native_scene,const char** failure=nullptr) noexcept {
    std::construct_at(&out);
    const auto check=[&](unsigned step){out.admission_step=static_cast<std::uint8_t>(step);if(failure)*failure=SapImage::AdmissionName(step);};
    check(1);
    const auto ptr=[](std::uintptr_t p,unsigned o=0){return ReadAt<std::uintptr_t>(reinterpret_cast<void*>(p),o);};
    const auto u32=[](std::uintptr_t p,unsigned o=0){return ReadAt<unsigned>(reinterpret_cast<void*>(p),o);};
    const auto u8=[](std::uintptr_t p,unsigned o=0){return ReadAt<unsigned char>(reinterpret_cast<void*>(p),o);};
    out.scene=scene;out.native_scene=native_scene;out.manager=ptr(scene,0x728);
    if(!out.manager)return false;
    const auto m=out.manager,s=out.broadphase=ptr(m,0x100);
    constexpr unsigned char getter[]{0x48,0x8b,0x81,0x80,1,0,0,0xc3};
    if(!s || ptr(s)!=module+0x1ace30 || ptr(module+0x1ace30,0x90)!=module+0x15a5e0
        || ptr(module+0x1aae40,8)!=module+0x1143e0
        || std::memcmp(reinterpret_cast<void*>(module+0x15a5e0),getter,sizeof(getter)))return false;
    check(2);
    // Native156BB0 has completed only when all six scratch owners are gone.
    unsigned detail=11;
    for(unsigned o:{0x150u,0x1a0u,0x1b0u,0x200u,0x248u,0x290u}) {
        check(detail++);
        if(ptr(s,o) || ptr(s,o+8))return false;
    }
    check(17);if(u32(s,0x1c0))return false;
    check(18);
    out.aggregate_slots=u32(m,0x1d8);out.aggregate_count=u32(m,0x1c8);out.aggregate_free_head=u32(m,0x1cc);
    if(out.aggregate_slots>SapImage::max_handles || out.aggregate_count>out.empty_aggregates.size()
        || out.aggregate_count>out.aggregate_slots)return false;
    if(out.aggregate_slots) {
        const auto table=ptr(m,0x1d0);
        if((u32(m,0x1dc)&0x7fffffffu)<out.aggregate_slots
            || !Writable(reinterpret_cast<void*>(table),out.aggregate_slots*8))return false;
        std::array<bool,SapImage::max_handles> free{};
        auto slot=out.aggregate_free_head;unsigned free_count{};
        while(slot!=0xffffffffu) {
            if(slot>=out.aggregate_slots || free[slot] || ptr(table,slot*8)>0xffffffffu)return false;
            free[slot]=true;++free_count;out.aggregate_free_links[slot]=u32(table,slot*8);slot=u32(table,slot*8);
        }
        if(free_count+out.aggregate_count!=out.aggregate_slots)return false;
        unsigned count{};
        for(unsigned i=0;i<out.aggregate_slots;++i)if(!free[i]) {
            const auto owner=ptr(table,i*8);
            if(!Writable(reinterpret_cast<void*>(owner),0x50) || u32(owner,0x10)
                || u32(owner,0x20)!=0xffffffffu)return false;
            const auto self=ptr(owner,0x18);
            // Native151690/152BB0 only consume this cache for nonempty/dirty
            // aggregates. Keep its allocation and timestamp native-owned.
            if(self && (!Writable(reinterpret_cast<void*>(self),0x50)
                || ptr(self)!=module+0x1acb50 || ptr(self,0x48)!=owner
                || u32(self,0x18) || u8(self,0x40)))return false;
            out.empty_aggregates[count++]={owner,self,i,u32(owner)};
        }
    } else if(out.aggregate_count || out.aggregate_free_head!=0xffffffffu)return false;
    detail=18;
    for(unsigned o:{0x1e8u,0x22cu,0x264u,0x118u,0x128u}) {
        check(++detail);if(u32(m,o))return false;
    }
    check(24);
    for(unsigned i=0;i<out.ids.size();++i) {
        auto& id=out.ids[i];id.owner=ptr(scene,0x1118+i*8);
        if(!Writable(reinterpret_cast<void*>(id.owner),0x38))return false;
        id.next=u32(id.owner);id.count=u32(id.owner,0x10);
        const auto free=ptr(id.owner,8),bitmap=ptr(id.owner,0x18);
        const auto capacity=u32(id.owner,0x14)&0x7fffffffu,words=u32(id.owner,0x20)&0x7fffffffu;
        if(id.next>SapImage::max_handles || id.count>id.next || id.count>capacity
            || u32(id.owner,0x30) || words>4096 || (words && !Writable(reinterpret_cast<void*>(bitmap),words*4))
            || (id.count && !Writable(reinterpret_cast<void*>(free),id.count*4)))return false;
        std::array<bool,SapImage::max_handles> seen{};
        for(unsigned j=0;j<id.count;++j) {
            id.free[j]=u32(free,j*4);
            if(id.free[j]>=id.next || seen[id.free[j]])return false;
            seen[id.free[j]]=true;
        }
        for(unsigned j=0;j<words;++j)if(u32(bitmap,j*4))return false;
    }
    check(28);
    // Native E8D30/F2810 creates a scene-lifetime StaticSim anchor before actors.
    // It owns rigid ID zero but no broadphase volume; no root state is restored.
    out.anchor_owner=ptr(scene,0x1138);
    if(!Writable(reinterpret_cast<void*>(out.anchor_owner),0x58)
        || ptr(out.anchor_owner)!=module+0x1aae70 || ptr(out.anchor_owner,0x40)!=scene
        || ptr(out.anchor_owner,0x28) || ptr(out.anchor_owner,0x30) || ptr(out.anchor_owner,0x38))return false;
    out.anchor_core=ptr(out.anchor_owner,0x48);out.anchor_id=u32(out.anchor_owner,0x50);
    if(out.anchor_id || !Writable(reinterpret_cast<void*>(out.anchor_core),0x30)
        || ptr(out.anchor_core)!=out.anchor_owner || u32(out.anchor_core,8)!=0xffffffu
        || u32(out.anchor_core,0xc)!=1 || u8(out.anchor_core,0x2c))return false;
    for(unsigned i=0;i<7;++i)if(u32(out.anchor_core,0x10+i*4)!=(i==3?0x3f800000u:0u))return false;
    out.extent=u32(m,0x1c0);out.boxes=u32(s,0x140);out.previous_boxes=u32(s,0x144);
    out.pair_count=u32(s,0x190);
    check(3);
    if(!out.extent || out.extent>SapImage::max_handles || out.boxes>out.extent
        || out.previous_boxes!=out.boxes || out.pair_count>SapImage::max_pairs
        || u32(s,0xc8)<out.extent || u32(s,0x148)<out.boxes*2+2)return false;
    // Native158990 restores these scratch permutations to canonical order.
    // Validate the whole current allocation so an older, larger extent can
    // reuse it without importing a historical scratch pointer or permutation.
    const auto links=u32(s,0x148);const auto next_links=ptr(s,0x130),previous_links=ptr(s,0x138);
    check(4);
    if(links>8192 || !next_links || !previous_links)return false;
    for(unsigned i=0;i<links;++i)
        if(u32(next_links,i*4)!=(i+1<links?i+1:i) || u32(previous_links,i*4)!=(i?i-1:0))return false;
    out.bounds_owner=ptr(m,0x108);out.distance_owner=ptr(m,0xa0);
    check(5);
    if(!out.bounds_owner || !out.distance_owner || u32(m,0xb0)<out.extent
        || u32(m,0x98)<out.extent || u32(out.bounds_owner,0x10)<out.extent
        || u32(out.distance_owner,0x10)<out.extent)return false;
    const auto volumes=ptr(m,0xa8),groups=ptr(m,0x90),bounds=ptr(out.bounds_owner,8),distance=ptr(out.distance_owner,8);
    if(!volumes || !groups || !bounds || !distance)return false;
    for(unsigned i=0;i<out.extent;++i) {
        check(6);
        auto& v=out.volumes[i];const auto tagged=ptr(volumes,i*16);
        if(tagged&15)return false;
        const auto aggregate=u32(volumes,i*16+8);
        v.shape=tagged;v.group=u32(groups,i*4);v.contact_distance=u32(distance,i*4);
        std::memcpy(v.bounds.data(),reinterpret_cast<void*>(bounds+i*24),24);
        if(aggregate!=0xffffffffu) {
            // Native14FAE0 tags an aggregate root as slot*2|1. Empty roots
            // own a handle but have no SAP endpoints or pending admission bits.
            if(!(aggregate&1) || !tagged || v.group==0xffffffffu || v.contact_distance)return false;
            const SapImage::EmptyAggregate* owner{};
            for(unsigned j=0;j<out.aggregate_count;++j)
                if(out.empty_aggregates[j].slot==aggregate/2 && out.empty_aggregates[j].handle==i)owner=&out.empty_aggregates[j];
            if(!owner)return false;
            v.empty_aggregate=true;v.core=owner->owner;v.actor=owner->self;
        } else if(v.shape) {
            if(ptr(v.shape)!=module+0x1aae40)return false;
            v.core=ptr(v.shape,0x40);v.actor=ptr(v.shape,0x10);
            v.shape_id=u32(v.shape,0x48);
            if((u32(v.shape,0x18)&0x1fffffffu)!=i)return false;
            if(!v.core || !v.actor || ptr(v.actor,0x40)!=scene || v.group==0xffffffffu)return false;
            v.rigid_id=u32(v.actor,0x50);
            reinterpret_cast<void(*)(void*,unsigned*,void*)>(module+0x1143e0)(
                reinterpret_cast<void*>(v.shape),&v.attributes,v.filter_data.data());
            if(!SapImage::ShapeAttributesSupported(v.attributes,v.filter_data[3]))return false;
        } else if(v.group!=0xffffffffu)return false;
    }
    // Native41140/41290 enumerate NpScene's rigid actor inventory. Static
    // 3ECE0->3FAF0 and dynamic39780 resolve Core/Sim through actor+80.
    // Never infer scene membership from the subset admitted to broadphase.
    check(6);
    if(!Writable(reinterpret_cast<void*>(native_scene),0x25a0) || ptr(native_scene)!=module+0x19d008)return false;
    const auto actor_count=u32(native_scene,0x2598);const auto actor_table=ptr(native_scene,0x2590);
    if(actor_count>SapImage::max_handles || actor_count>(u32(native_scene,0x259c)&0x7fffffffu)
        || (actor_count && !Writable(reinterpret_cast<void*>(actor_table),actor_count*8)))return false;
    std::array<bool,SapImage::max_handles> visited{};
    for(unsigned i=0;i<actor_count;++i) {
        const auto native=ptr(actor_table,i*8);
        if(!Writable(reinterpret_cast<void*>(native),0x90))return false;
        const auto kind=ReadAt<unsigned short>(reinterpret_cast<void*>(native),8);
        if((kind!=6 && kind!=7) || ptr(native)!=module+(kind==6?0x19b7c0:0x19be60))return false;
        const auto actor=ptr(native,0x80);
        if(!actor)continue; // A registered actor may have simulation disabled.
        if(!Writable(reinterpret_cast<void*>(actor),0x58) || ptr(actor,0x40)!=scene || ptr(actor,0x48)!=native+0x80)return false;
        const auto rigid_id=u32(actor,0x50);
        if(rigid_id>=SapImage::max_handles || out.rigid_members[rigid_id].simulation)return false;
        out.rigid_members[rigid_id]={native,actor};
        auto element=ptr(actor,0x38);unsigned count{};
        while(element) {
            if(++count>SapImage::max_handles || !Writable(reinterpret_cast<void*>(element),0x50)
                || ptr(element)!=module+0x1aae40 || ptr(element,0x10)!=actor)return false;
            const auto id=u32(element,0x18)&0x1fffffffu;
            if(id>=visited.size() || visited[id])return false;
            visited[id]=true;
            if(id<out.extent && out.volumes[id].shape) {
                if(out.volumes[id].empty_aggregate || out.volumes[id].shape!=element)return false;
            } else {
                auto& v=out.non_broadphase[id];v.shape=element;v.actor=actor;v.core=ptr(element,0x40);
                if(!Writable(reinterpret_cast<void*>(v.core),0x48))return false;
                v.flags=u8(v.core,0x40);if(v.flags&5)return false;
                v.shape_id=u32(element,0x48);v.rigid_id=rigid_id;v.contact_distance=u32(v.core,0x3c);
                reinterpret_cast<void(*)(void*,unsigned*,void*)>(module+0x1143e0)(
                    reinterpret_cast<void*>(element),&v.attributes,v.filter_data.data());
            }
            element=ptr(element,8);
        }
    }
    for(unsigned i=0;i<out.extent;++i)
        if(out.volumes[i].shape && !out.volumes[i].empty_aggregate && !visited[i])return false;
    for(unsigned j=0;j<out.aggregate_count;++j) {
        const auto& a=out.empty_aggregates[j];
        if(a.handle>=out.extent || !out.volumes[a.handle].empty_aggregate
            || out.volumes[a.handle].core!=a.owner)return false;
    }
    for(unsigned axis=0;axis<3;++axis) {
        check(7);
        const auto boxes=ptr(s,0xd0+axis*8),values=ptr(s,0xe8+axis*8),owners=ptr(s,0x100+axis*8);
        if(!boxes || !values || !owners)return false;
        std::memcpy(out.box_endpoints[axis].data(),reinterpret_cast<void*>(boxes),out.extent*8);
        std::memcpy(out.values[axis].data(),reinterpret_cast<void*>(values),(out.boxes*2+2)*4);
        std::memcpy(out.owners[axis].data(),reinterpret_cast<void*>(owners),(out.boxes*2+2)*4);
        const auto o=0x48+axis*0x10;const auto bitmap=ptr(m,o);const auto words=u32(m,o+8)&0x7fffffffu;
        if(words<SapImage::max_handles/32 && words*32<out.extent)return false;
        if(!bitmap || words>4096)return false;
        for(unsigned j=0;j<words;++j) {
            const auto value=u32(bitmap,j*4);
            if(j<out.pending[axis].size())out.pending[axis][j]=value;
            else if(value)return false;
            if(value && (j*32>=out.extent || (j*32+32>out.extent && value>>(out.extent-j*32))))return false;
        }
    }
    const auto pairs=ptr(s,0x180),states=ptr(s,0x188),hash=ptr(s,0x160),next=ptr(s,0x168);
    check(8);
    const auto hash_size=u32(s,0x170),hash_capacity=u32(s,0x174),pair_capacity=u32(s,0x194);
    if(!pairs || !states || !hash || !next || !hash_size || (hash_size&(hash_size-1))
        || hash_size>hash_capacity || hash_capacity>4096 || pair_capacity>4096
        || out.pair_count>pair_capacity || u32(s,0x198)!=hash_size-1)return false;
    std::array<bool,SapImage::max_pairs> seen{};
    check(9);
    for(unsigned bucket=0;bucket<hash_size;++bucket) {
        auto index=u32(hash,bucket*4);
        while(index!=SapImage::invalid) {
            if(index>=out.pair_count || seen[index])return false;
            if((PhysicsSapHash(u32(pairs,index*16),u32(pairs,index*16+4))&(hash_size-1))!=bucket)return false;
            seen[index]=true;index=u32(next,index*4);
        }
    }
    for(unsigned i=0;i<out.pair_count;++i) {
        if(!seen[i])return false;
        auto& pair=out.pairs[i];pair.handles={u32(pairs,i*16),u32(pairs,i*16+4)};
        pair.interaction=ptr(pairs,i*16+8);pair.state=u8(states,i);
    }
    out.bounds_changed=u8(out.bounds_owner,0x18);out.origin_shifted=u8(m,0x1c4);out.persistent_changed=u8(m,0x1c5);
    if(const auto missing=out.IdCoverageFailure()){check(missing);return false;}
    check(10);
    out.valid=out.WellFormed();if(out.valid)check(0);return out.valid;
}

bool PhysicsSapStorage(const SapImage& desired,const SapImage& current) noexcept {
    if(desired.extent>current.extent || !desired.SameOwners(current) || !desired.WellFormed() || !current.WellFormed())return false;
    const auto ptr=[](std::uintptr_t p,unsigned o=0){return ReadAt<std::uintptr_t>(reinterpret_cast<void*>(p),o);};
    const auto u32=[](std::uintptr_t p,unsigned o=0){return ReadAt<unsigned>(reinterpret_cast<void*>(p),o);};
    const auto s=current.broadphase,m=current.manager;
    // Native freeBuffers may shrink pair allocations, but never below this
    // minimum. A and retained B must fit that floor before native execution.
    if(desired.pair_count>u32(s,0x178) || u32(s,0x178)>u32(s,0x194)
        || u32(s,0xc8)<desired.extent || u32(s,0x148)<desired.boxes*2+2)return false;
    struct Range{std::uintptr_t start;std::size_t bytes;};
    std::array<Range,28> ranges{};unsigned count{};
    const auto add=[&](std::uintptr_t p,std::size_t size) {
        if(!p || !Writable(reinterpret_cast<void*>(p),size))return false;
        for(unsigned i=0;i<count;++i) {
            const auto& r=ranges[i];if(p>=r.start?p-r.start<r.bytes:r.start-p<size)return false;
        }
        ranges[count++]={p,size};return true;
    };
    for(unsigned axis=0;axis<3;++axis)
        if(!add(ptr(s,0xd0+axis*8),desired.extent*8)
            || !add(ptr(s,0xe8+axis*8),(desired.boxes*2+2)*4)
            || !add(ptr(s,0x100+axis*8),(desired.boxes*2+2)*4)
            || !add(ptr(m,0x48+axis*16),(u32(m,0x50+axis*16)&0x7fffffffu)*4))return false;
    for(unsigned i=0;i<current.ids.size();++i) {
        const auto owner=current.ids[i].owner;
        if(!add(owner,0x38))return false;
        const auto required=(std::max)(desired.ids[i].count,current.ids[i].count);
        if(required>(u32(owner,0x14)&0x7fffffffu) || (required && !add(ptr(owner,8),required*4)))return false;
    }
    return add(ptr(s,0x160),u32(s,0x170)*4) && add(ptr(s,0x168),u32(s,0x194)*4)
        && add(ptr(s,0x180),u32(s,0x194)*16) && add(ptr(s,0x188),u32(s,0x194))
        && add(ptr(current.bounds_owner,8),desired.extent*24)
        && Writable(reinterpret_cast<void*>(s+0x140),8)
        && Writable(reinterpret_cast<void*>(s+0x190),4)
        && Writable(reinterpret_cast<void*>(m+0x1c4),2)
        && Writable(reinterpret_cast<void*>(current.bounds_owner+0x18),1);
}

// Signed shifts match native156BB0/161xxx. Rebuild into current hash backing;
// native capacities and all allocation addresses stay owned by native code.
unsigned PhysicsSapHash(unsigned a,unsigned b) noexcept {
    auto v=(b<<16)|a;v+=~(v<<15);v=(static_cast<std::int32_t>(v)>>10^v)*9;
    v^=static_cast<std::int32_t>(v)>>6;v+=~(v<<11);
    return static_cast<std::int32_t>(v)>>16^v;
}
template<class Remap>
bool WritePhysicsSap(const SapImage& desired,const SapImage& current,Remap remap) noexcept {
    if(!PhysicsSapStorage(desired,current))return false;
    const auto ptr=[](std::uintptr_t p,unsigned o=0){return ReadAt<std::uintptr_t>(reinterpret_cast<void*>(p),o);};
    const auto u32=[](std::uintptr_t p,unsigned o=0){return ReadAt<unsigned>(reinterpret_cast<void*>(p),o);};
    const auto put=[](std::uintptr_t p,unsigned o,auto value){std::memcpy(reinterpret_cast<void*>(p+o),&value,sizeof(value));};
    std::array<std::uintptr_t,SapImage::max_pairs> mapped{};
    for(unsigned i=0;i<desired.pair_count;++i) {
        mapped[i]=remap(desired.pairs[i].interaction);
        if(desired.pairs[i].interaction && !mapped[i])return false;
    }
    const auto s=current.broadphase,m=current.manager;
    for(unsigned axis=0;axis<3;++axis) {
        std::memcpy(reinterpret_cast<void*>(ptr(s,0xd0+axis*8)),desired.box_endpoints[axis].data(),desired.extent*8);
        std::memcpy(reinterpret_cast<void*>(ptr(s,0xe8+axis*8)),desired.values[axis].data(),(desired.boxes*2+2)*4);
        std::memcpy(reinterpret_cast<void*>(ptr(s,0x100+axis*8)),desired.owners[axis].data(),(desired.boxes*2+2)*4);
        const auto bitmap=ptr(m,0x48+axis*16);const auto words=u32(m,0x50+axis*16)&0x7fffffffu;
        for(unsigned j=0;j<words;++j)put(bitmap,j*4,j<desired.pending[axis].size()?desired.pending[axis][j]:0u);
    }
    const auto bounds=ptr(current.bounds_owner,8);
    for(unsigned i=0;i<desired.extent;++i)
        std::memcpy(reinterpret_cast<void*>(bounds+i*24),desired.volumes[i].bounds.data(),24);
    const auto hash=ptr(s,0x160),next=ptr(s,0x168),pairs=ptr(s,0x180),states=ptr(s,0x188);
    const auto mask=u32(s,0x170)-1;
    for(unsigned i=0;i<=mask;++i)put(hash,i*4,SapImage::invalid);
    for(unsigned i=0;i<desired.pair_count;++i) {
        const auto& p=desired.pairs[i];const auto bucket=PhysicsSapHash(p.handles[0],p.handles[1])&mask;
        put(pairs,i*16,p.handles[0]);put(pairs,i*16+4,p.handles[1]);put(pairs,i*16+8,mapped[i]);
        put(states,i,p.state);put(next,i*4,u32(hash,bucket*4));put(hash,bucket*4,i);
    }
    // Active ID-to-owner bindings are unchanged; install only the logical
    // next/free continuation through independently validated current backing.
    // The immutable B image remains available after A/C execution and growth.
    for(unsigned i=1;i<desired.ids.size();++i) {
        const auto& id=desired.ids[i];const auto owner=current.ids[i].owner;
        if(id.count)std::memcpy(reinterpret_cast<void*>(ptr(owner,8)),id.free.data(),id.count*4);
        put(owner,0,id.next);put(owner,0x10,id.count);
    }
    put(s,0x140,desired.boxes);put(s,0x144,desired.previous_boxes);put(s,0x190,desired.pair_count);
    put(m,0x1c4,desired.origin_shifted);put(m,0x1c5,desired.persistent_changed);
    put(current.bounds_owner,0x18,desired.bounds_changed);
    return true;
}
