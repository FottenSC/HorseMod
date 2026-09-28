// Included by Sc6ReplayParticleCopy.cpp inside Horse::Deterministic.
// The enclosing completed-application/RHI owner excludes all native writers.
namespace {
std::uint64_t VisibilitySamplingTableHash(std::uintptr_t base)
{
    // Static initializer14021D990 generates3571 samples from its own fixed
    // seed0x83246. Neither peer observations nor future inputs seed this table.
    std::uint64_t hash=1469598103934665603ull;
    for(unsigned i=0;i<3571;++i) hash=(hash^Field<unsigned>(base,0x432db84+std::size_t(i)*4))*1099511628211ull;
    return hash;
}
bool VisibilityWritable(std::uintptr_t address,std::size_t bytes) noexcept
{
    if(!bytes) return true;
    if(!address || bytes>UINTPTR_MAX-address) return false;
    const auto end=address+bytes;
    while(address<end) {
        MEMORY_BASIC_INFORMATION region{};
        if(!VirtualQuery(reinterpret_cast<void*>(address),&region,sizeof(region)) || region.State!=MEM_COMMIT
            || (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
        const auto access=region.Protect&0xff;
        if(access!=PAGE_READWRITE && access!=PAGE_WRITECOPY && access!=PAGE_EXECUTE_READWRITE && access!=PAGE_EXECUTE_WRITECOPY) return false;
        const auto next=reinterpret_cast<std::uintptr_t>(region.BaseAddress)+region.RegionSize;
        if(next<=address) return false;
        address=next;
    }
    return true;
}
}

bool Sc6ReplayParticleCopy::RetainedUniformBytes(void* value,std::size_t& bytes) const noexcept
{
    __try {
        bytes=0;
        if(!value) return true;
        // Native141206AB0 owns this0x58-byte wrapper and its resource TArray;
        // a retained reference prevents its D3D buffer returning to the pool.
        const auto wrapper=reinterpret_cast<std::uintptr_t>(value);
        if(Field<std::uintptr_t>(wrapper)!=base_+0x35d3dc0 || Field<int>(wrapper,8)<1
            || Field<int>(wrapper,0x48)<0 || Field<int>(wrapper,0x4c)<Field<int>(wrapper,0x48)
            || Field<int>(wrapper,0x4c)>64) return false;
        const auto layout=Field<std::uintptr_t>(wrapper,0x18);
        if(!layout || Field<unsigned>(layout)>65536 || Field<unsigned>(layout,0x10)!=unsigned(Field<int>(wrapper,0x48))) return false;
        auto* buffer=Field<ID3D11Buffer*>(wrapper,0x20);
        D3D11_BUFFER_DESC descriptor{};
        if(buffer) {
            buffer->GetDesc(&descriptor);
            if(!(descriptor.BindFlags&D3D11_BIND_CONSTANT_BUFFER) || descriptor.ByteWidth<Field<unsigned>(layout)
                || descriptor.ByteWidth>65536) return false;
        } else if(Field<unsigned>(layout)) return false;
        const auto quantify=reinterpret_cast<std::size_t(*)(std::size_t,unsigned)>(base_+0xd50dc0);
        bytes=descriptor.ByteWidth+quantify(0x58,0);
        if(Field<int>(wrapper,0x4c)) bytes+=quantify(std::size_t(Field<int>(wrapper,0x4c))*8,0);
        // Referenced authored/cache textures remain owned by the retained
        // native scene/assets. This charges this image's uniform allocation;
        // it does not claim those textures were copied or made replay-owned.
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

// Native141F46720/141F45D50 and1414C82E0 publish two native references:
// the instance's render resource and scene GUID map. Retained RHI references
// protect immutable uniform contents from native pool reuse, with no GPU copy.
bool Sc6ReplayParticleCopy::ResolveMaterialPublication(std::uintptr_t& instance,std::uintptr_t& resource,std::uintptr_t& scene_slot) const noexcept
{
    __try {
        instance=resource=scene_slot=0;
        const auto battle=reinterpret_cast<std::uintptr_t(*)(void*)>(base_+0x3ef7a0)(reinterpret_cast<void*>(world_));
        const auto collection=battle?Field<std::uintptr_t>(battle,0x1420):0;
        const int count=Field<int>(world_,0x150);
        if(!collection || count<1 || count>128) return false;
        const auto data=Field<std::uintptr_t>(world_,0x148);
        if(!data) return false;
        for(int i=0;i<count;++i) {
            const auto object=Field<std::uintptr_t>(data,std::size_t(i)*8);
            if(object && Field<std::uintptr_t>(object,0x30)==collection) {
                if(instance || Field<std::uintptr_t>(object,0x38)!=world_) return false;
                instance=object;
            }
        }
        if(!instance) return false;
        resource=Field<std::uintptr_t>(instance,0xe0);
        if(!resource || !VisibilityWritable(resource+0x10,8)
            || std::memcmp(reinterpret_cast<void*>(resource),reinterpret_cast<void*>(collection+0x28),16)) return false;
        const auto map=scene_+0x5830;
        const int entries=Field<int>(map,8),bits=Field<int>(map,0x28);
        const auto records=Field<std::uintptr_t>(map),heap=Field<std::uintptr_t>(map,0x20);
        const auto flags=heap?heap:map+0x10;
        if(entries<1 || entries>128 || bits!=entries || Field<int>(map,0xc)<entries
            || Field<int>(map,0x2c)<bits || (!heap && Field<int>(map,0x2c)>128) || !records) return false;
        for(int i=0;i<entries;++i) {
            if(!(Field<unsigned>(flags,std::size_t(i/32)*4)&(1u<<(i%32)))) continue;
            const auto entry=records+std::size_t(i)*0x20;
            if(std::memcmp(reinterpret_cast<void*>(entry),reinterpret_cast<void*>(resource),16)) continue;
            if(scene_slot || !VisibilityWritable(entry+0x10,8)) return false;
            scene_slot=entry+0x10;
        }
        return scene_slot!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool Sc6ReplayParticleCopy::PublishMaterial(bool original,bool preflight_only) noexcept
{
    __try {
        if(!view_state_) return true;
        std::uintptr_t instance{},resource{},slot{};
        if(!ResolveMaterialPublication(instance,resource,slot)) return false;
        const auto matches=[&](const VisibilityImage& image) {
            return image.material_instance==instance && image.material_resource==resource
                && !std::memcmp(image.material_guid.data(),reinterpret_cast<void*>(resource),16)
                && Field<void*>(resource,0x10)==image.material_uniform
                && Field<void*>(slot)==image.material_scene_uniform;
        };
        if(!original && !material_dirty_) return matches(visibility_b_);
        if(visibility_a_.material_instance!=instance || visibility_b_.material_instance!=instance
            || visibility_a_.material_resource!=resource || visibility_b_.material_resource!=resource
            || visibility_a_.material_guid!=visibility_b_.material_guid) return false;
        if(original && (material_dirty_ || !matches(visibility_b_))) return false;
        if(preflight_only) return true;
        const auto& image=original?visibility_a_:visibility_b_;
        const auto assign=reinterpret_cast<void**(*)(void**,void*)>(base_+0x15b83f0);
        // Both B references remain independently leased even if only the
        // first native slot changes. Undo tolerates this partial publication.
        if(original) material_dirty_=true;
        assign(reinterpret_cast<void**>(resource+0x10),image.material_uniform);
        assign(reinterpret_cast<void**>(slot),image.material_scene_uniform);
        if(!matches(image)) return false;
        if(!original) material_dirty_=false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool Sc6ReplayParticleCopy::VisibilityAdmission() const noexcept
{
    if(!view_state_) return true;
    __try {
        if(Field<std::uintptr_t>(view_state_)!=base_+0x3657810
            || Field<int>(base_,0x40904fc)!=1 || Field<int>(base_,0x432d570)!=0) return false;
        // The alternate primitive-fetch worker also consumes the shared
        // sample cursor. Do not silently serialize it or presume its worker
        // ordering deterministic. This bounded participant admits the native
        // serial branch only; the setting is observed, never modified.
        if(Field<int>(base_,0x432d578)!=0 && Field<bool>(base_,0x43445cd)) return false;
        if(Field<unsigned>(base_,0x432db80)>3571 || !VisibilityWritable(base_+0x432db80,4)) return false;
        const auto setting=Field<std::uintptr_t>(base_,0x4335d18);
        if(!setting || Field<int>(setting,4)!=0
            || Field<std::uintptr_t>(view_state_,0x108)!=base_+0x360c728
            || Field<int>(view_state_,0x120)!=1
            || Field<std::uintptr_t>(view_state_,0xf8)!=0
            || Field<int>(view_state_,0x100)!=2 || Field<int>(view_state_,0x104)!=2) return false;
        // All four maps were empty in the assembled A/B ownership observation.
        // This is an explicit bounded admission condition, not an instruction
        // to clear histories or select a different combat interval.
        for(const auto offset:{0x58u,0xa8u,0x950u,0x9b0u}) {
            const auto map=view_state_+offset;
            if(Field<std::uintptr_t>(map) || Field<int>(map,8) || Field<int>(map,0xc)
                || Field<std::uintptr_t>(map,0x20) || Field<int>(map,0x28)
                || Field<std::uintptr_t>(map,0x40) || Field<int>(map,0x48)) return false;
        }
        return VisibilityWritable(view_state_+0x1020,0x50)
            && VisibilityWritable(view_state_+0x1b0,0x36c) && VisibilityWritable(view_state_+0x520,0x36c)
            && VisibilityWritable(view_state_+0x898,0xc) && VisibilityWritable(view_state_+0x8b0,0x50)
            && VisibilityWritable(view_state_+0xff0,0x2c) && VisibilityWritable(view_state_+0x124,4);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool Sc6ReplayParticleCopy::CaptureVisibility(VisibilityImage& image,std::size_t budget) noexcept
{
    try {return CaptureVisibilityProtected(image,budget);}
    catch(...) {return false;}
}
bool Sc6ReplayParticleCopy::CaptureVisibilityProtected(VisibilityImage& image,std::size_t budget)
{
    __try {return CaptureVisibilityUnchecked(image,budget);}
    __except(GetExceptionCode()==0xe06d7363u?EXCEPTION_CONTINUE_SEARCH:EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool Sc6ReplayParticleCopy::CaptureVisibilityUnchecked(VisibilityImage& image,std::size_t budget)
{
    if(!view_state_) return true;
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] visibility sampling admission target={} parallel_fetch={} platform_threads={} cursor={}\n"),
        &image==&visibility_a_,Field<int>(base_,0x432d578),Field<bool>(base_,0x43445cd),Field<unsigned>(base_,0x432db80));
    if(image.captured || !image.entries.empty() || image.leases || !VisibilityAdmission()) return false;
    std::uintptr_t material_slot{};
    if(!ResolveMaterialPublication(image.material_instance,image.material_resource,material_slot)) return false;
    std::memcpy(image.material_guid.data(),reinterpret_cast<void*>(image.material_resource),16);
    auto* material=Field<void*>(image.material_resource,0x10);
    auto* scene_material=Field<void*>(material_slot);
    if(!material || !scene_material || material!=scene_material) return false;
    std::size_t material_bytes{},scene_material_bytes{};
    if(!RetainedUniformBytes(material,material_bytes) || !RetainedUniformBytes(scene_material,scene_material_bytes)) return false;
    material_bytes+=scene_material_bytes; // Conservative duplicate-reference charge.
    if(witness_.bytes>budget || material_bytes>budget-witness_.bytes) return false;
    witness_.bytes+=material_bytes;
    const auto assign=reinterpret_cast<void**(*)(void**,void*)>(base_+0x15b83f0);
    assign(&image.material_uniform,material);assign(&image.material_scene_uniform,scene_material);
    const auto map=view_state_+0x1020;
    const int count=Field<int>(map,8),capacity=Field<int>(map,0xc);
    const int bits=Field<int>(map,0x28),bit_capacity=Field<int>(map,0x2c);
    const int free_count=Field<int>(map,0x34),hash_count=Field<int>(map,0x48);
    const auto data=Field<std::uintptr_t>(map),flag_heap=Field<std::uintptr_t>(map,0x20),hash_heap=Field<std::uintptr_t>(map,0x40);
    if(count<0 || count>8192 || capacity<count || capacity>8192 || bits!=count || bit_capacity<bits
        || bit_capacity>8192 || bit_capacity%32 || (!flag_heap && bit_capacity>128)
        || free_count<0 || free_count>count || hash_count<1 || hash_count>16384
        || (hash_count&(hash_count-1)) || (!hash_heap && hash_count!=1)) return false;
    const auto flags=flag_heap?flag_heap:map+0x10,hashes=hash_heap?hash_heap:map+0x38;
    const std::size_t entry_bytes=std::size_t(count)*0x50,flag_bytes=std::size_t(bit_capacity)/8,hash_bytes=std::size_t(hash_count)*4;
    const std::size_t reserved=entry_bytes+flag_bytes+hash_bytes+std::size_t(count)*2*(sizeof(void*)+0x30);
    if(witness_.bytes>budget || reserved>budget-witness_.bytes
        || !VisibilityWritable(data,std::size_t(capacity)*0x50) || !VisibilityWritable(flags,flag_bytes)
        || !VisibilityWritable(hashes,hash_bytes)) return false;
    // Reserve before allocation so a partial allocation failure remains
    // charged while the enclosing failure path retains this participant.
    witness_.bytes+=reserved;
    image.entries.resize(entry_bytes);image.flags.resize(flag_bytes);image.hashes.resize(hash_bytes);
    image.queries.reserve(std::size_t(count)*2);
    // Charge retained query wrappers conservatively per possible slot, even
    // when multiple slots share an owner. No new D3D query is allocated here.
    const auto charged=image.entries.capacity()+image.flags.capacity()+image.hashes.capacity()
        +image.queries.capacity()*(sizeof(void*)+0x30);
    if(charged>reserved) return false;
    if(!ReadBytes(image.header.data(),reinterpret_cast<void*>(map),0x50)
        || !ReadBytes(image.entries.data(),reinterpret_cast<void*>(data),entry_bytes)
        || !ReadBytes(image.flags.data(),reinterpret_cast<void*>(flags),flag_bytes)
        || !ReadBytes(image.hashes.data(),reinterpret_cast<void*>(hashes),hash_bytes)) return false;
    unsigned active{};
    for(int i=0;i<count;++i) {
        if(!(Field<unsigned>(flags,std::size_t(i/32)*4)&(1u<<(i%32)))) continue;
        ++active;
        const auto entry=data+std::size_t(i)*0x50;
        const int ring_count=Field<int>(entry,0x20),ring_capacity=Field<int>(entry,0x24);
        if(Field<std::uintptr_t>(entry,0x18) || ring_count<1 || ring_count>2 || ring_capacity!=2) return false;
        for(int j=0;j<ring_count;++j) {
            const auto query=Field<std::uintptr_t>(entry,8+std::size_t(j)*8);
            if(!query) continue;
            if(Field<std::uintptr_t>(query)!=base_+0x35d3db8 || Field<int>(query,8)<1 || Field<int>(query,8)>INT_MAX-16384
                || !Field<std::uintptr_t>(query,0x18) || Field<int>(query,0x2c)!=1) return false;
            const int pooled=Field<int>(view_state_,0x118),pool_capacity=Field<int>(view_state_,0x11c);
            const auto pool_data=Field<std::uintptr_t>(view_state_,0x110);
            if(pooled<0 || pool_capacity<pooled || pool_capacity>8192 || (pooled && !pool_data)) return false;
            for(int k=0;k<pooled;++k) if(Field<std::uintptr_t>(pool_data,std::size_t(k)*8)==query) return false;
            image.queries.push_back(reinterpret_cast<void*>(query));
        }
    }
    if(active!=static_cast<unsigned>(count-free_count)) return false;
    // Every active resource and all storage are validated before acquisition.
    for(auto* query:image.queries) {
        InterlockedIncrement(&Field<LONG>(reinterpret_cast<std::uintptr_t>(query),8));++image.leases;
    }
    if(!ReadBytes(image.previous_matrices.data(),reinterpret_cast<void*>(view_state_+0x1b0),0x36c)
        || !ReadBytes(image.current_matrices.data(),reinterpret_cast<void*>(view_state_+0x520),0x36c)
        || !ReadBytes(image.times.data(),reinterpret_cast<void*>(view_state_+0x898),0xc)
        || !ReadBytes(image.camera_epoch.data(),reinterpret_cast<void*>(view_state_+0x8b0),0x50)
        || !ReadBytes(image.lod.data(),reinterpret_cast<void*>(view_state_+0xff0),0x2c)) return false;
    image.pool_bookkeeping=Field<int>(view_state_,0x124);
    image.query_sampling_cursor=Field<unsigned>(base_,0x432db80);
    image.query_sampling_table=VisibilitySamplingTableHash(base_);
    image.captured=true;
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] visibility retained target={} slots={} query_refs={} view_epoch={} bytes={}\n"),
        &image==&visibility_a_,count,image.leases,Field<unsigned>(view_state_,0x8fc),charged);
    return true;
}

bool Sc6ReplayParticleCopy::PrepareVisibilityStorage(std::size_t budget) noexcept
{
    if(!view_state_) return true;
    if(visibility_storage_ready_) return !visibility_dirty_ && !visibility_storage_transferred_;
    if(visibility_dirty_ || !visibility_a_.captured || !visibility_b_.captured) return false;
    for(const auto* p:visibility_installed_) if(p) return false;
    __try {
        struct Range {std::uintptr_t address;std::size_t bytes;};
        std::array<Range,3> prior{};
        std::array<std::size_t,3> sizes{};
        std::size_t charge{};
        const auto quantify=reinterpret_cast<std::size_t(*)(std::size_t,unsigned)>(base_+0xd50dc0);
        const auto a=reinterpret_cast<std::uintptr_t>(visibility_a_.header.data());
        const auto b=reinterpret_cast<std::uintptr_t>(visibility_b_.header.data());
        const auto overlap=[](Range x,Range y) {
            if(!x.bytes || !y.bytes) return false;
            if(!x.address || !y.address || x.bytes>UINTPTR_MAX-x.address || y.bytes>UINTPTR_MAX-y.address) return true;
            return x.address<y.address+y.bytes && y.address<x.address+x.bytes;
        };
        for(unsigned i=0;i<3;++i) {
            const auto offset=i==0?0:i==1?0x20:0x40;
            const auto extent=[&](std::uintptr_t h) -> std::size_t {
                if(i==0) return std::size_t(Field<unsigned>(h,0xc))*0x50;
                if(i==1) return std::size_t(Field<unsigned>(h,0x2c))/8;
                return std::size_t(Field<unsigned>(h,0x48))*4;
            };
            sizes[i]=(i==0 || Field<void*>(a,offset))?extent(a):0;
            prior[i]={Field<std::uintptr_t>(b,offset), (i==0 || Field<void*>(b,offset))?extent(b):0};
            if(overlap(prior[i],{view_state_+0x1020,0x50})) return false;
            for(unsigned j=0;j<i;++j) if(overlap(prior[i],prior[j])) return false;
            for(auto* query:visibility_b_.queries)
                if(overlap(prior[i],{reinterpret_cast<std::uintptr_t>(query),0x30})) return false;
            for(const auto bytes:{sizes[i],prior[i].bytes}) if(bytes) {
                const auto native=quantify(bytes,0);
                if(native<bytes || charge>SIZE_MAX-native) return false;
                charge+=native;
            }
        }
        if(witness_.bytes>budget || charge>budget-witness_.bytes) return false;
        witness_.bytes+=charge;
        for(unsigned i=0;i<3;++i) if(sizes[i]) {
            auto* p=reinterpret_cast<void*(*)(std::size_t)>(base_+0x4a61c0)(sizes[i]);
            if(!p) return false;
            visibility_installed_[i]=p;
            std::memset(p,0,sizes[i]);
            for(const auto old:prior) if(overlap({reinterpret_cast<std::uintptr_t>(p),sizes[i]},old)) {
                // Ownership is ambiguous; ordinary cleanup must not free B.
                capture_owner_fault_=true;return false;
            }
        }
        visibility_storage_ready_=true;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool Sc6ReplayParticleCopy::PruneOrphanVisibility(std::size_t budget) noexcept
{
    if(!view_state_) return true;
    if(!completion_.retired() || visibility_storage_ready_ || visibility_dirty_ || visibility_native_a_refs_
        || !visibility_a_.captured || !visibility_b_.captured || !VisibilityMatches(visibility_b_)) return false;
    constexpr std::size_t scratch=17*1024;
    if(witness_.bytes>budget || scratch>budget-witness_.bytes) return false;
    witness_.bytes+=scratch; // Conservative peak charge for bounded stack planning.
    __try {
        constexpr unsigned char signature[]{0x89,0x54,0x24,0x10,0x48,0x83,0xec,0x28,0x8b,0x41,0x08,0x4c,0x8b,0xd9,0x3b,0x41,0x34};
        if(std::memcmp(reinterpret_cast<void*>(base_+0x14f5290),signature,sizeof(signature))) return false;
        for(auto* q:visibility_a_.queries) {
            const auto p=reinterpret_cast<std::uintptr_t>(q);
            if(Field<std::uintptr_t>(p)!=base_+0x35d3db8 || Field<int>(p,8)<2 || Field<int>(p,0x2c)!=1) return false;
        }
        const auto orphan=[&](unsigned id,unsigned subquery) {
            if(subquery) return false;
            for(const auto* image:{&lighting_a_,&lighting_b_})
                for(const auto& row:image->primitives) if(row.id==id) return false;
            for(std::size_t i=0;i<creation_render_owner_count_;++i)
                if(creation_render_owners_[i].primitive_id==id) return false;
            return true;
        };
        auto& image=visibility_a_;
        const auto result=ReplayOcclusionHistory::Prune(image.header,image.entries,image.flags,image.hashes,
            image.queries,image.leases,orphan,[&](void* map,int slot) {
                reinterpret_cast<void(*)(void*,int)>(base_+0x14f5290)(map,slot);
            });
        if(result.removed) RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] visibility orphan reconstruction removed={} query_leases_released={} private_A=true B_unchanged=true native_erase=true\n"),result.removed,result.queries_released);
        return result.ok && VisibilityMatches(visibility_b_);
    } __except(EXCEPTION_EXECUTE_HANDLER) {capture_owner_fault_=true;return false;}
}

#include "Sc6ReplayParticleCopy.VisibilityRelease.inl"







bool Sc6ReplayParticleCopy::SettleVisibilityExecution(std::size_t budget) noexcept
{
    if(!view_state_) return true;
    if(!visibility_executing_ || visibility_execution_settled_ || !visibility_dirty_ || !visibility_storage_published_)
        return false;
    if(!visibility_c_.captured && !CaptureVisibility(visibility_c_,budget)) return false;
    if(!VisibilityMatches(visibility_c_)
        || visibility_c_.material_instance!=visibility_b_.material_instance
        || visibility_c_.material_resource!=visibility_b_.material_resource
        || visibility_c_.material_guid!=visibility_b_.material_guid) return false;
    __try {
        const auto c=reinterpret_cast<std::uintptr_t>(visibility_c_.header.data());
        const auto b=reinterpret_cast<std::uintptr_t>(visibility_b_.header.data());
        struct Range {std::uintptr_t address;std::size_t bytes;};
        const auto overlap=[](Range x,Range y) {
            if(!x.bytes || !y.bytes) return false;
            if(!x.address || !y.address || x.bytes>UINTPTR_MAX-x.address || y.bytes>UINTPTR_MAX-y.address) return true;
            return x.address<y.address+y.bytes && y.address<x.address+x.bytes;
        };
        std::array<Range,3> old{},live{};
        std::size_t charge{};
        for(unsigned i=0;i<3;++i) {
            const auto offset=i==0?0:i==1?0x20:0x40;
            const auto range=[&](std::uintptr_t h) -> Range {
                const auto pointer=Field<std::uintptr_t>(h,offset);
                return {pointer,pointer?(i==0?std::size_t(Field<unsigned>(h,0xc))*0x50:
                    i==1?std::size_t(Field<unsigned>(h,0x2c))/8:std::size_t(Field<unsigned>(h,0x48))*4):0};
            };
            old[i]=range(b);live[i]=range(c);
            const auto native=live[i].bytes?reinterpret_cast<std::size_t(*)(std::size_t,unsigned)>(base_+0xd50dc0)(live[i].bytes,0):0;
            if(native<live[i].bytes || charge>SIZE_MAX-native) return false;
            charge+=native;
        }
        if(witness_.bytes>budget || charge>budget-witness_.bytes) return false;
        for(unsigned i=0;i<3;++i) {
            if(overlap(live[i],{view_state_+0x1020,0x50})) return false;
            for(unsigned j=0;j<3;++j) if(overlap(live[i],old[j]) || (i!=j && overlap(live[i],live[j]))) return false;
            for(auto* query:visibility_b_.queries) if(overlap(live[i],{reinterpret_cast<std::uintptr_t>(query),0x30})) return false;
            for(auto* query:visibility_c_.queries) if(overlap(live[i],{reinterpret_cast<std::uintptr_t>(query),0x30})) return false;
        }
        // B's original native allocations were never handed to C. Verify
        // their used payload before adopting any address for retirement.
        const auto flags=Field<std::uintptr_t>(b,0x20),hashes=Field<std::uintptr_t>(b,0x40);
        if((!visibility_b_.entries.empty() && std::memcmp(reinterpret_cast<void*>(Field<std::uintptr_t>(b)),visibility_b_.entries.data(),visibility_b_.entries.size()))
            || std::memcmp(reinterpret_cast<void*>(flags?flags:b+0x10),visibility_b_.flags.data(),visibility_b_.flags.size())
            || std::memcmp(reinterpret_cast<void*>(hashes?hashes:b+0x38),visibility_b_.hashes.data(),visibility_b_.hashes.size())) return false;
        if(!ReleaseVisibilityImage(visibility_a_)) return false;
        witness_.bytes+=charge;
        visibility_a_=std::move(visibility_c_);
        // Vector move does not clear scalar lease counts or raw uniform
        // pointers. Ownership transferred to A; C must not release it again.
        visibility_c_.leases=0;
        visibility_c_.material_uniform=visibility_c_.material_scene_uniform=nullptr;
        visibility_c_.captured=false;
        for(unsigned i=0;i<3;++i) visibility_installed_[i]=reinterpret_cast<void*>(live[i].address);
        // These references already belong to the live C map. Undo retires
        // them only after B is published; commit leaves them native-owned.
        visibility_native_a_refs_=visibility_a_.queries.size();
        visibility_execution_settled_=true;
        return VisibilityMatches(visibility_a_);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool Sc6ReplayParticleCopy::VisibilityMatches(const VisibilityImage& image) const noexcept
{
    if(!view_state_) return true;
    __try {
        if(!image.captured || !VisibilityAdmission() || image.leases!=image.queries.size()) return false;
        const auto map=view_state_+0x1020;
        auto header=image.header;
        if(&image==&visibility_a_ && visibility_storage_published_)
            for(unsigned i=0;i<3;++i) Field<void*>(reinterpret_cast<std::uintptr_t>(header.data()),i==0?0:i==1?0x20:0x40)=visibility_installed_[i];
        if(std::memcmp(reinterpret_cast<void*>(map),header.data(),0x50)) return false;
        const auto flags=Field<std::uintptr_t>(map,0x20),hashes=Field<std::uintptr_t>(map,0x40);
        if(std::memcmp(reinterpret_cast<void*>(Field<std::uintptr_t>(map)),image.entries.data(),image.entries.size())
            || std::memcmp(reinterpret_cast<void*>(flags?flags:map+0x10),image.flags.data(),image.flags.size())
            || std::memcmp(reinterpret_cast<void*>(hashes?hashes:map+0x38),image.hashes.data(),image.hashes.size())) return false;
        for(auto* query:image.queries) {
            const auto q=reinterpret_cast<std::uintptr_t>(query);
            if(Field<std::uintptr_t>(q)!=base_+0x35d3db8 || Field<int>(q,8)<2 || Field<int>(q,0x2c)!=1) return false;
        }
        return !std::memcmp(reinterpret_cast<void*>(view_state_+0x1b0),image.previous_matrices.data(),0x36c)
            && !std::memcmp(reinterpret_cast<void*>(view_state_+0x520),image.current_matrices.data(),0x36c)
            && !std::memcmp(reinterpret_cast<void*>(view_state_+0x898),image.times.data(),0xc)
            && !std::memcmp(reinterpret_cast<void*>(view_state_+0x8b0),image.camera_epoch.data(),0x50)
            && !std::memcmp(reinterpret_cast<void*>(view_state_+0xff0),image.lod.data(),0x2c)
            && Field<int>(view_state_,0x124)==image.pool_bookkeeping
            && Field<unsigned>(base_,0x432db80)==image.query_sampling_cursor
            && VisibilitySamplingTableHash(base_)==image.query_sampling_table;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

#define VISIBILITY_REJECT() do { RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] visibility publication rejected line={} original={} preflight={}\n"),__LINE__,original,preflight_only); return false; } while(false)
bool Sc6ReplayParticleCopy::PublishVisibility(bool original,bool preflight_only) noexcept
{
    if(!view_state_) return true;
    if(visibility_executing_ && !visibility_execution_settled_) VISIBILITY_REJECT();
    __try {
        // A rejected preflight has not changed B. In particular, changed
        // native backing allocations must not prevent recovery of untouched
        // B merely because those allocations differ from the old A image.
        if(!original && !visibility_dirty_ && !visibility_native_a_refs_)
            return VisibilityMatches(visibility_b_);
        if(!VisibilityAdmission() || !visibility_a_.captured || !visibility_b_.captured) VISIBILITY_REJECT();
        if(visibility_a_.query_sampling_table!=visibility_b_.query_sampling_table
            || VisibilitySamplingTableHash(base_)!=visibility_b_.query_sampling_table) VISIBILITY_REJECT();
        const auto a=reinterpret_cast<std::uintptr_t>(visibility_a_.header.data());
        const auto b=reinterpret_cast<std::uintptr_t>(visibility_b_.header.data());
        const auto map=view_state_+0x1020;
        // A owns separate backing. B retains its original native allocations
        // throughout publication and execution, including different capacities.
        if(!visibility_storage_ready_ || visibility_storage_transferred_) VISIBILITY_REJECT();
        const auto& image=original?visibility_a_:visibility_b_;
        auto header=image.header;
        if(original) for(unsigned i=0;i<3;++i)
            Field<void*>(reinterpret_cast<std::uintptr_t>(header.data()),i==0?0:i==1?0x20:0x40)=visibility_installed_[i];
        const auto h=reinterpret_cast<std::uintptr_t>(header.data());
        const auto data=Field<std::uintptr_t>(h),flag_heap=Field<std::uintptr_t>(h,0x20),hash_heap=Field<std::uintptr_t>(h,0x40);
        const auto flags=flag_heap?flag_heap:map+0x10,hashes=hash_heap?hash_heap:map+0x38;
        if(!VisibilityWritable(data,std::size_t(Field<unsigned>(h,0xc))*0x50)
            || !VisibilityWritable(flags,image.flags.size()) || !VisibilityWritable(hashes,image.hashes.size())) VISIBILITY_REJECT();
        if(original) {
            if(visibility_dirty_ || visibility_native_a_refs_ || !VisibilityMatches(visibility_b_)) VISIBILITY_REJECT();
            // Historical keys must still have membership in B. The world
            // participant owns the B-only particle; this map cannot resurrect
            // an independently destroyed primitive or bypass its lifetime.
            for(int i=0;i<Field<int>(a,8);++i) {
                const auto af=reinterpret_cast<std::uintptr_t>(visibility_a_.flags.data());
                if(!(Field<unsigned>(af,std::size_t(i/32)*4)&(1u<<(i%32)))) continue;
                const auto ae=reinterpret_cast<std::uintptr_t>(visibility_a_.entries.data())+std::size_t(i)*0x50;
                bool found=false;
                for(int j=0;j<Field<int>(b,8);++j) {
                    const auto bf=reinterpret_cast<std::uintptr_t>(visibility_b_.flags.data());
                    if(!(Field<unsigned>(bf,std::size_t(j/32)*4)&(1u<<(j%32)))) continue;
                    const auto be=reinterpret_cast<std::uintptr_t>(visibility_b_.entries.data())+std::size_t(j)*0x50;
                    if(Field<unsigned>(ae)==Field<unsigned>(be) && Field<unsigned>(ae,0x40)==Field<unsigned>(be,0x40)) {found=true;break;}
                }
                if(!found) {
                    // Native1414FF1D0 trims history independently of UObject
                    // lifetime. A missing B cache key is not an expired owner.
                    // Serial1414CF550 uses (scene primitive ID, subquery index).
                    // Admit only the main query for a independently retained,
                    // currently bound or natively reconstructible primitive.
                    const auto id=Field<unsigned>(ae),subquery=Field<unsigned>(ae,0x40);
                    const auto primitive=std::find_if(lighting_a_.primitives.begin(),lighting_a_.primitives.end(),
                        [&](const auto& row){return row.id==id;});
                    if(subquery==0 && primitive!=lighting_a_.primitives.end()) {
                        if(primitive->binding==LightingPrimitive::Binding::DormantCreation)
                            found=DormantCreationBinding(*primitive);
                        else if(primitive->binding==LightingPrimitive::Binding::DormantTrace)
                            found=TraceRenderBinding(*primitive);
                        else if(primitive->binding==LightingPrimitive::Binding::PendingTraceTarget)
                            found=PendingTraceTargetBinding(*primitive);
                        else if(primitive->binding==LightingPrimitive::Binding::PendingStageTarget)
                            found=PendingStageTargetBinding(*primitive);
                        else found=std::any_of(lighting_b_.primitives.begin(),lighting_b_.primitives.end(),
                            [&](const auto& row){return row.id==id && row.component==primitive->component && row.weak==primitive->weak;});
                    }
                    if(!found && subquery==0 && primitive==lighting_a_.primitives.end()) {
                        // A historical query can predate A's active proxy.
                        // Native trimming of B's cache does not invalidate a
                        // still-live B primitive. Reuse the existing complete
                        // weak/component/proxy/scene-membership validation;
                        // cached metadata alone must never grant ownership.
                        const auto current=std::find_if(lighting_b_.primitives.begin(),lighting_b_.primitives.end(),
                            [&](const auto& row){return row.id==id && row.binding==LightingPrimitive::Binding::ScenePrimitive;});
                        if(current!=lighting_b_.primitives.end())found=LightingBindings(lighting_b_);
                    }
                    if(!found && subquery==0) {
                        // Occlusion history can predate a component becoming
                        // hidden, so A need not have an active scene proxy.
                        // Resolve through the existing A/B component owner,
                        // including its captured immutable primitive ID; no
                        // extra historical proxy or query result is fabricated.
                        const auto end=creation_render_owners_.begin()+creation_render_owner_count_;
                        const auto owner=std::find_if(creation_render_owners_.begin(),end,
                            [&](const auto& row){return row.primitive_id==id;});
                        if(owner!=end) {
                            LightingPrimitive binding{};
                            binding.component=owner->component;binding.weak=owner->weak;
                            binding.id=owner->primitive_id;binding.proxy_type=base_+0x39af350;
                            binding.slot_count=1;binding.proxy_inputs[0]=owner->mesh;
                            found=DormantCreationBinding(binding,false);
                        }
                    }
                    if(!found) {
                        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] visibility owner rejected id={} subquery={} before_A_publication=true\n"),id,subquery);
                        VISIBILITY_REJECT();
                    }
                }
            }
            for(auto* query:visibility_a_.queries) {
                const auto q=reinterpret_cast<std::uintptr_t>(query);
                if(Field<std::uintptr_t>(q)!=base_+0x35d3db8 || Field<int>(q,8)<1 || Field<int>(q,0x2c)!=1) VISIBILITY_REJECT();
            }
            if(preflight_only) return true;
            // B's native references stay owned until commit. They are complete
            // undo even if any later publication or GPU operation fails.
            visibility_dirty_=true;
            visibility_storage_published_=true;
            for(auto* query:visibility_a_.queries) {
                InterlockedIncrement(&Field<LONG>(reinterpret_cast<std::uintptr_t>(query),8));++visibility_native_a_refs_;
            }
        } else if(!visibility_dirty_) {
            if(visibility_native_a_refs_) VISIBILITY_REJECT();
            return VisibilityMatches(visibility_b_);
        }
        if(!ReadBytes(reinterpret_cast<void*>(data),image.entries.data(),image.entries.size())
            || !ReadBytes(reinterpret_cast<void*>(flags),image.flags.data(),image.flags.size())
            || !ReadBytes(reinterpret_cast<void*>(hashes),image.hashes.data(),image.hashes.size())
            || !ReadBytes(reinterpret_cast<void*>(map),header.data(),0x50)
            || !ReadBytes(reinterpret_cast<void*>(view_state_+0x1b0),image.previous_matrices.data(),0x36c)
            || !ReadBytes(reinterpret_cast<void*>(view_state_+0x520),image.current_matrices.data(),0x36c)
            || !ReadBytes(reinterpret_cast<void*>(view_state_+0x898),image.times.data(),0xc)
            || !ReadBytes(reinterpret_cast<void*>(view_state_+0x8b0),image.camera_epoch.data(),0x50)
            || !ReadBytes(reinterpret_cast<void*>(view_state_+0xff0),image.lod.data(),0x2c)) VISIBILITY_REJECT();
        Field<int>(view_state_,0x124)=image.pool_bookkeeping;
        Field<unsigned>(base_,0x432db80)=image.query_sampling_cursor;
        // +8FC is this retained view's history/maintenance epoch. Global
        // engine/render epochs and Family-derived +890/+894 remain current.
        // Empty shadow/fade maps above make that separation admissible here.
        if(!VisibilityMatches(image)) VISIBILITY_REJECT();
        if(!original) {
            while(visibility_native_a_refs_) {
                auto* query=visibility_a_.queries[visibility_native_a_refs_-1];
                if(Field<LONG>(reinterpret_cast<std::uintptr_t>(query),8)<2) VISIBILITY_REJECT();
                InterlockedDecrement(&Field<LONG>(reinterpret_cast<std::uintptr_t>(query),8));--visibility_native_a_refs_;
            }
            visibility_dirty_=false;
            visibility_storage_published_=false;
        }
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] visibility published target={} verified=true view_epoch={}\n"),original,Field<unsigned>(view_state_,0x8fc));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {VISIBILITY_REJECT();}
}

#undef VISIBILITY_REJECT

bool Sc6ReplayParticleCopy::FinishVisibility(bool commit) noexcept
{
    if(visibility_executing_ && !visibility_execution_settled_) return false;
    __try {
        if(commit) {
            if(!view_state_) return true;
            if(!material_dirty_) return false;
            std::uintptr_t instance{},resource{},slot{};
            if(!ResolveMaterialPublication(instance,resource,slot)
                || instance!=visibility_a_.material_instance || resource!=visibility_a_.material_resource
                || Field<void*>(resource,0x10)!=visibility_a_.material_uniform
                || Field<void*>(slot)!=visibility_a_.material_scene_uniform) return false;
            if(!visibility_dirty_ || visibility_native_a_refs_!=visibility_a_.queries.size() || !VisibilityMatches(visibility_a_)) return false;
            for(auto* query:visibility_b_.queries)
                if(Field<LONG>(reinterpret_cast<std::uintptr_t>(query),8)<2) return false;
            // Each B slot still has its native reference and a separate undo
            // lease, so dropping native ownership cannot destroy a resource.
            for(auto* query:visibility_b_.queries) InterlockedDecrement(&Field<LONG>(reinterpret_cast<std::uintptr_t>(query),8));
            visibility_native_a_refs_=0; // transferred to the installed map
            visibility_dirty_=false;
            material_dirty_=false;
            const auto free=reinterpret_cast<void(*)(void*)>(base_+0xd46a00);
            const auto h=reinterpret_cast<std::uintptr_t>(visibility_b_.header.data());
            for(const auto offset:{0u,0x20u,0x40u}) if(auto* p=Field<void*>(h,offset)) {free(p);Field<void*>(h,offset)=nullptr;}
            visibility_installed_={};visibility_storage_transferred_=true;visibility_storage_published_=false;
            return true;
        }
        if(visibility_dirty_ || visibility_native_a_refs_ || material_dirty_) return false;
        for(auto*& p:visibility_installed_) if(p) {reinterpret_cast<void(*)(void*)>(base_+0xd46a00)(p);p=nullptr;}
        for(auto* image:{&visibility_a_,&visibility_b_,&visibility_c_}) if(!ReleaseVisibilityImage(*image)) return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {capture_owner_fault_=true;return false;}
}
