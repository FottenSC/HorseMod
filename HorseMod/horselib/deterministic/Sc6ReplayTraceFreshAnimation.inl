static bool ValidatePreparedChildAnimation(const FreshChild& child,const State& historical) noexcept {
    __try {
        auto* instance=child.state.animation.object;auto* proxy=child.animation_proxy;
        if(!instance || !historical.animation.object || !proxy || At<void*>(instance,0x350)!=proxy
            || At<void*>(proxy,0xa0)!=instance || At<void*>(proxy,0xa8)!=child.animation_metadata
            || child.animation_arrays.empty())return false;
        const auto old=reinterpret_cast<std::uintptr_t>(historical.animation.object);
        const auto fresh=reinterpret_cast<std::uintptr_t>(instance);
        for(std::size_t i=0;i<child.animation_arrays.size();++i) {
            const auto& row=child.animation_arrays[i];
            const auto a=reinterpret_cast<std::uintptr_t>(row.historical),b=reinterpret_cast<std::uintptr_t>(row.destination);
            if(a<old || b<fresh || a-old!=b-fresh || a-old>INT_MAX || (i==0 && a-old!=0x3f0))return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
struct ChildAnimationRows {void* destination{};Array source{};const std::byte* bytes{};void* historical{};};
static bool WriteChildAnimation(std::uintptr_t base,Object* animation,const ChildAnimationRows* rows,std::size_t count) noexcept {
    __try {
        // Preserve the native parallel-evaluation fence/proxy publication path.
        // This first establishes native destinations; each destination then
        // receives its own retained A rows, which need not match the primary.
        reinterpret_cast<void(*)(void*,const Array*)>(base+0x8d5450)(animation,&rows[0].source);
        for(std::size_t i=0;i<count;++i) {
            const auto& row=rows[i];const auto before=At<Array>(row.destination,0);
            if(before.count<0 || before.capacity<before.count || before.capacity>1024 || (before.capacity&&!before.data))return false;
            reinterpret_cast<void(*)(void*,const void*,int,int,int)>(base+0x8c8a50)(row.destination,
                row.source.data,row.source.count,before.capacity,row.source.capacity-row.source.count);
            const auto after=At<Array>(row.destination,0);
            if(after.count!=row.source.count || after.capacity!=row.source.capacity || (after.capacity&&!after.data))return false;
            // Native copy skips padding and spare rows. They are actual retained
            // A bytes here, not independent expected observations. Keep the
            // private destination's complete captured backing reproducible.
            if(after.capacity)std::memcpy(after.data,row.bytes,std::size_t(after.capacity)*0x70);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
Status PrepareFreshChildAnimation(const ChildSource& source,const Sc6ReplayTraceState& current,
    FreshChild& child,std::size_t budget) const {
    if(thread_!=GetCurrentThreadId() || !child.prepared || child.retired || !child.identity_retained
        || child.history_phase!=FreshChild::HistoryPhase::Ready || child.animation_phase!=FreshChild::AnimationPhase::Empty
        || child.historical.state!=source.historical.state || child.historical.controller!=source.historical.controller)
        return Fail("fresh_child_animation_context");
    auto status=current.ValidateValues();if(!status.ok())return status;
    if(!child.lease.Validate().ok())return Fail("fresh_child_animation_lease");
    status=current.CheckChildStorageAndLifecycle(child.state,current,current);if(!status.ok())return status;
    const auto historical=std::find_if(states_.begin(),states_.end(),[&](const auto& s) {
        return s.ref.state==source.historical.state && s.ref.controller==source.historical.controller;
    });
    if(historical==states_.end() || !historical->animation.object
        || child.state.animation.object->GetClassPrivate()!=source.animation_class
        || At<Object*>(child.state.animation.object,0x20)!=child.state.mesh.object
        || At<void*>(child.state.mesh.object,0xec8))return Fail("fresh_child_animation_owner");
    void* old_proxy{};void* old_nodes{};void* old_metadata{};
    if(!RetainedField(bindings_,historical->animation.object,0x350,old_proxy) || !old_proxy
        || !RetainedField(bindings_,old_proxy,0xa0,old_nodes)
        || !RetainedField(bindings_,old_proxy,0xa8,old_metadata))return Fail("fresh_child_animation_proxy_source");
    auto* proxy=At<void*>(child.state.animation.object,0x350);
    // The admitted native creator1408CEAF0 passes the animation instance to
    // 1408C9560/140441650, which stores that exact owner at proxy+A0.
    // This is owned UObject node storage, not an arbitrary non-null base.
    if(!proxy || old_nodes!=historical->animation.object || At<void*>(proxy,0xa0)!=child.state.animation.object
        || At<void*>(proxy,0xa8)!=old_metadata || At<std::uintptr_t>(proxy,0)!=base_+0x3360058
        || At<std::uintptr_t>(At<void*>(child.state.animation.object,0),0x298)!=base_+0x8ceaf0)
        return Fail("fresh_child_animation_proxy_identity");
    std::array<ChildAnimationRows,257> rows{};std::size_t count{},allocation_bytes{};
    if(budget<sizeof(rows))return Status::failure(FailureCode::CapacityExceeded);
    const auto add=[&](void* destination,void* old_header) {
        for(std::size_t i=0;i<count;++i)if(rows[i].destination==destination)return false;
        if(count==rows.size())return false;
        Array source_rows{};
        if(!RetainedField(bindings_,old_header,0,source_rows.data)
            || !RetainedField(values_,old_header,8,source_rows.count)
            || !RetainedField(bindings_,old_header,12,source_rows.capacity)
            || source_rows.count<0 || source_rows.capacity<source_rows.count || source_rows.capacity>1024)return false;
        const auto bytes=std::size_t(source_rows.capacity)*0x70;
        const auto* payload=bytes?RetainedSpan(values_,source_rows.data,0,bytes):nullptr;
        if(bytes&&!payload)return false;
        // A bounded native allocation reservation. Overall native/GPU peak
        // accounting is still a separate qualification requirement.
        if(bytes>4u*1024*1024-allocation_bytes || bytes>budget-sizeof(rows)
            || allocation_bytes>budget-sizeof(rows)-bytes)return false;
        const auto fresh=At<Array>(destination,0);
        if(fresh.count<0 || fresh.capacity<fresh.count || fresh.capacity>1024 || (fresh.capacity&&!fresh.data))return false;
        source_rows.data=const_cast<std::byte*>(payload);
        rows[count++]={destination,source_rows,payload,old_header};allocation_bytes+=bytes;return true;
    };
    if(!add(reinterpret_cast<std::byte*>(child.state.animation.object)+0x3f0,
        reinterpret_cast<std::byte*>(historical->animation.object)+0x3f0))return Fail("fresh_child_animation_primary_source");
    if(old_metadata) {
        auto* properties=reinterpret_cast<void*(*)(void*)>(At<std::uintptr_t>(At<void*>(old_metadata,0),0x20))(old_metadata);
        const auto* header=RetainedSpan(bindings_,properties,0,16);
        if(!header || std::memcmp(header,properties,16))return Fail("fresh_child_animation_properties");
        const auto props=At<Array>(properties,0);
        if(props.count<0 || props.count>256 || props.capacity<props.count)return Fail("fresh_child_animation_property_count");
        const auto bytes=std::size_t(props.count)*8;
        const auto* entries=bytes?RetainedSpan(bindings_,props.data,0,bytes):nullptr;
        if(bytes && (!entries || std::memcmp(entries,props.data,bytes)))return Fail("fresh_child_animation_property_identity");
        auto* type=reinterpret_cast<void*(*)()>(base_+0x8e4340)();
        for(int i=0;i<props.count;++i) {
            auto* node=reinterpret_cast<void*(*)(void*,int,void*)>(base_+0x1c93e00)(proxy,i,type);
            if(!node)continue;
            const auto address=reinterpret_cast<std::uintptr_t>(node),origin=At<std::uintptr_t>(proxy,0xa0);
            const auto previous=reinterpret_cast<std::uintptr_t>(old_nodes);
            // 141C93E00 returns proxy+A0 plus the shared property's signed
            // offset at+50. Translate that typed node offset, never scan words.
            if(!origin || !previous || address<origin || address-origin>INT_MAX
                || previous>UINTPTR_MAX-(address-origin)-0x88)return Fail("fresh_child_animation_node_offset");
            auto* destination=reinterpret_cast<std::byte*>(node)+0x78;
            auto* old_header=reinterpret_cast<void*>(previous+(address-origin)+0x78);
            if(destination==reinterpret_cast<std::byte*>(child.state.animation.object)+0x3f0) {
                if(old_header!=reinterpret_cast<std::byte*>(historical->animation.object)+0x3f0)return Fail("fresh_child_animation_primary_alias");
                continue;
            }
            if(!add(destination,old_header))return Fail("fresh_child_animation_node_source");
        }
    }
    constexpr unsigned char publish[]{0x40,0x57,0x41,0x57,0x48,0x83,0xec,0x38,0x48,0x89,0x5c,0x24,0x50,0x48,0x8b,0xfa};
    const auto initial_bytes=count*std::size_t(rows[0].source.count)*0x70;
    const auto metadata_bytes=count*sizeof(FreshChild::AnimationDestination);
    if(metadata_bytes>budget-sizeof(rows) || initial_bytes>4u*1024*1024 || initial_bytes>budget-sizeof(rows)-metadata_bytes
        || allocation_bytes>budget-sizeof(rows)-metadata_bytes-initial_bytes)return Status::failure(FailureCode::CapacityExceeded);
    constexpr unsigned char copy[]{0x48,0x89,0x5c,0x24,8,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
    if(std::memcmp(reinterpret_cast<void*>(base_+0x8d5450),publish,sizeof(publish))
        || std::memcmp(reinterpret_cast<void*>(base_+0x8c8a50),copy,sizeof(copy)))return Fail("fresh_child_animation_native_signature");
    if(!child.animation_arrays.empty() || count*sizeof(FreshChild::AnimationDestination)>budget-sizeof(rows))
        return Fail("fresh_child_animation_destination_journal");
    try {
        child.animation_arrays.reserve(count);
        for(std::size_t i=0;i<count;++i)child.animation_arrays.push_back({rows[i].historical,rows[i].destination});
    } catch(...) {return Status::failure(FailureCode::CapacityExceeded);}
    child.animation_proxy=proxy;child.animation_metadata=old_metadata;
    if(!ValidatePreparedChildAnimation(child,*historical))return Fail("fresh_child_animation_destination_owner");
    child.animation_phase=FreshChild::AnimationPhase::Calling;
    if(!WriteChildAnimation(base_,child.state.animation.object,rows.data(),count)) {
        child.animation_phase=FreshChild::AnimationPhase::Failed;return Fail("fresh_child_animation_native_write");
    }
    if(!ValidatePreparedChildAnimation(child,*historical)) {
        child.animation_phase=FreshChild::AnimationPhase::Failed;return Fail("fresh_child_animation_destination_changed");
    }
    status=current.ValidateValues();
    if(!status.ok()) {child.animation_phase=FreshChild::AnimationPhase::Failed;return status;}
    child.animation_phase=FreshChild::AnimationPhase::Ready;
    return status;
}
