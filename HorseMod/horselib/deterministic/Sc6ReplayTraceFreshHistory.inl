struct ChildHistoryWrite {void* destination{};const std::byte* source{};std::size_t bytes{};};
static bool ChildHistoryWritable(const ChildHistoryWrite& write) noexcept {
    auto address=reinterpret_cast<std::uintptr_t>(write.destination);
    if(!address || !write.source || !write.bytes || write.bytes>UINTPTR_MAX-address)return false;
    const auto end=address+write.bytes;
    while(address<end) {
        MEMORY_BASIC_INFORMATION region{};
        if(!VirtualQuery(reinterpret_cast<void*>(address),&region,sizeof(region)) || region.State!=MEM_COMMIT
            || (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))
            || !(region.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
        const auto next=reinterpret_cast<std::uintptr_t>(region.BaseAddress)+region.RegionSize;
        if(next<=address)return false;
        address=next;
    }
    return true;
}
static bool WriteChildHistory(std::uintptr_t base,std::byte* state,const Ref& parent,
    const ChildHistoryWrite* writes,std::size_t count) noexcept {
    __try {
        // This becomes the state's native cached-parent weak ownership. Its
        // ordinary destructor releases it; no parent strong owner is acquired.
        if(parent.controller && !reinterpret_cast<ReplayTraceWeakController*>(parent.controller)->RetainLiveIdentity(base+0x3362590))return false;
        At<Ref>(state,0xa0)=parent;
        for(std::size_t i=0;i<count;++i)std::memcpy(writes[i].destination,writes[i].source,writes[i].bytes);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
Status PrepareFreshChildHistory(const ChildSource& source,const Sc6ReplayTraceState& current,FreshChild& child) const {
    if(thread_!=GetCurrentThreadId() || !child.prepared || child.retired || !child.identity_retained
        || child.history_phase!=FreshChild::HistoryPhase::Empty
        || child.factory.phase!=Sc6ReplayTraceChildFactory::Phase::Returned
        || child.historical.state!=source.historical.state || child.historical.controller!=source.historical.controller)
        return Fail("fresh_child_history_context");
    auto status=current.ValidateValues();if(!status.ok())return status;
    if(!child.lease.Validate().ok())return Fail("fresh_child_history_lease");
    if(child.state.ref.state!=child.factory.reference.state || child.state.ref.controller!=child.factory.reference.controller
        || current.Contains(child.state.ref) || !child.state.ref.controller || child.state.ref.state!=child.state.ref.controller+16
        || At<std::uintptr_t>(child.state.ref.controller,0)!=base_+0x3362590
        || At<int>(child.state.ref.controller,8)!=1 || At<int>(child.state.ref.controller,12)<2)
        return Fail("fresh_child_history_native_identity");
    status=current.CheckChildStorageAndLifecycle(child.state,current,current);if(!status.ok())return status;
    auto* state=child.state.ref.state;
    if(At<Ref>(state,0xa0).state || At<Ref>(state,0xa0).controller)return Fail("fresh_child_history_parent_reuse");
    Ref parent{};
    if(!RetainedField(bindings_,source.historical.state,0xa0,parent))return Fail("fresh_child_history_parent_source");
    if(parent.controller) {
        const auto previous=std::find_if(states_.begin(),states_.end(),[&](const auto& s){return s.ref.state==parent.state && s.ref.controller==parent.controller;});
        const auto now=std::find_if(current.states_.begin(),current.states_.end(),[&](const auto& s){return s.ref.state==parent.state && s.ref.controller==parent.controller;});
        if(previous==states_.end() || now==current.states_.end() || previous->actor!=now->actor || previous->mesh!=now->mesh
            || previous->animation!=now->animation || previous->attachment!=now->attachment
            || parent.state!=parent.controller+16 || At<std::uintptr_t>(parent.controller,0)!=base_+0x3362590
            || At<int>(parent.controller,8)<=0 || At<int>(parent.controller,12)<=0 || At<int>(parent.controller,12)==INT_MAX)
            return Fail("fresh_child_history_parent_identity");
    } else if(parent.state)return Fail("fresh_child_history_parent_pair");
    std::array<ChildHistoryWrite,15> writes{};std::size_t count{};
    const auto add=[&](void* destination,const void* historical,std::size_t offset,std::size_t bytes) {
        if(count==writes.size())return false;
        ChildHistoryWrite write{destination,RetainedSpan(values_,historical,offset,bytes),bytes};
        if(!ChildHistoryWritable(write))return false;
        writes[count++]=write;return true;
    };
    // Exactly the pointer-free state fields already owned by trace capture.
    // Native actor/asset/array bindings and all allocation capacities stay fresh.
    for(const auto range:{std::pair{0u,2u},{0x20u,0x35u},{0x80u,8u},{0x98u,4u},
        {0xb0u,4u},{0xc0u,9u},{0xccu,0x24u},{0xf0u,1u},{0xf4u,4u}})
        if(!add(state+range.first,source.historical.state,range.first,range.second))return Fail("fresh_child_history_scalar");
    for(const auto offset:{0x10u,0x70u,0x88u}) {
        Array captured{};
        if(!RetainedField(bindings_,source.historical.state,offset,captured.data)
            || !RetainedField(bindings_,source.historical.state,offset+12,captured.capacity)
            || !RetainedField(values_,source.historical.state,offset+8,captured.count))return Fail("fresh_child_history_array_source");
        const auto fresh=At<Array>(state,offset);
        if(captured.count<0 || captured.capacity<captured.count || captured.capacity>1024
            || fresh.capacity!=captured.capacity || fresh.count<0 || fresh.count>fresh.capacity
            || (fresh.capacity && (!fresh.data || !captured.data)))return Fail("fresh_child_history_array_shape");
        const auto bytes=std::size_t(captured.capacity)*(offset==0x10?0x70:0x2c);
        if(bytes && !add(fresh.data,captured.data,0,bytes))return Fail("fresh_child_history_array_payload");
        if(!add(state+offset+8,source.historical.state,offset+8,4))return Fail("fresh_child_history_array_count");
    }
    child.history_phase=FreshChild::HistoryPhase::Writing;
    if(!WriteChildHistory(base_,state,parent,writes.data(),count)) {
        child.history_phase=FreshChild::HistoryPhase::Failed;return Fail("fresh_child_history_write");
    }
    child.cached_parent=parent;
    child.history_phase=FreshChild::HistoryPhase::Ready;
    return current.ValidateValues();
}
