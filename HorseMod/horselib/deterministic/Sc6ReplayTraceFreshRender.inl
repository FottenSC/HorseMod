// Validate the native world queues before allowing the constructor's deferred
// render work to run. B has no pending render work at this boundary; every
// queued component must therefore be an explicitly retained fresh child.
static bool FreshChildRenderQueue(std::uintptr_t base,void* world,
    std::span<FreshChild* const> children,bool drained) noexcept {
    __try {
        std::array<unsigned,16> membership{};
        auto* lock=reinterpret_cast<CRITICAL_SECTION*>(static_cast<std::byte*>(world)+0x270);
        EnterCriticalSection(lock);
        __try {
            for(unsigned set=0;set<2;++set) {
                auto* h=static_cast<std::byte*>(world)+0x1d0+set*0x50;
                const auto slots=At<int>(h,8),capacity=At<int>(h,12),free=At<int>(h,0x34);
                if(slots<0 || capacity<slots || capacity>65536 || free<0 || free>slots
                    || At<int>(h,0x28)!=slots || At<int>(h,0x2c)<slots)return false;
                auto* bits=At<unsigned*>(h,0x20);
                if(!bits) {if(slots>128)return false;bits=reinterpret_cast<unsigned*>(h+0x10);}
                auto* entries=At<std::byte*>(h,0);
                if(slots&&!entries)return false;
                unsigned count{};
                for(int i=0;i<slots;++i)if((bits[i/32]>>(i%32))&1u) {
                    if(drained)return false;
                    auto* component=reinterpret_cast<Object*(*)(const void*)>(base+0xf823f0)(entries+i*16);
                    std::size_t n{};while(n<children.size() && children[n]->state.mesh.object!=component)++n;
                    if(n==children.size() || membership[n])return false;
                    membership[n]=set+1;++count;
                }
                if(count!=static_cast<unsigned>(slots-free))return false;
            }
            for(std::size_t i=0;i<children.size();++i) {
                const auto* child=children[i];auto* mesh=child->state.mesh.object;
                if(!child->lease.Validate().ok() || At<void*>(mesh,0x1c8)!=world
                    || At<void*>(mesh,0xec8))return false;
                const auto flags=At<unsigned>(mesh,0x188);
                if((flags&3)!=3 || (flags>>30)!=membership[i]
                    || (drained?(flags&0xe0)!=0:bool(flags&0xe0)!=bool(membership[i])))return false;
            }
            return true;
        } __finally {LeaveCriticalSection(lock);}
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
static bool RunFreshChildRenderWork(std::uintptr_t base,void* world) noexcept {
    __try {
        constexpr unsigned char signature[]{0x40,0x55,0x57,0x48,0x8d,0x6c,0x24,0xb1,0x48,0x81,0xec,0xb8,0,0,0,0x8b};
        if(std::memcmp(reinterpret_cast<void*>(base+0x1efeba0),signature,sizeof(signature)))return false;
        reinterpret_cast<void(*)(void*)>(base+0x1efeba0)(world);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
static bool SetFreshChildVisibility(std::uintptr_t base,void* mesh,unsigned visibility) noexcept {
    __try {
        constexpr unsigned char signature[]{0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x48,0x8d,0x6c,0x24,0xb9,0x48,0x81,0xec,0,1,0,0};
        if(std::memcmp(reinterpret_cast<void*>(base+0x1dad440),signature,sizeof(signature)))return false;
        // Native SetVisibility(self, visible, no propagation) performs its
        // required render-dirty notification. The world flush follows below.
        reinterpret_cast<void(*)(void*,unsigned char,unsigned char)>(base+0x1dad440)(mesh,(visibility>>4)&1,0);
        return (At<unsigned>(mesh,0x240)&0x10)==(visibility&0x10);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
Status DrainFreshChildRenderWork(const Sc6ReplayTraceState& current,std::span<FreshChild* const> children) const {
    if(thread_!=GetCurrentThreadId() || base_!=current.base_ || world_!=current.world_
        || children.empty() || children.size()>16)return Fail("fresh_child_render_context");
    auto status=current.ValidateValues();if(!status.ok())return status;
    for(const auto* child:children)
        if(!child || !child->prepared || child->retired || !child->identity_retained
            || child->animation_phase!=FreshChild::AnimationPhase::Ready || !child->lease.Validate().ok())
            return Fail("fresh_child_render_owner");
    if(!FreshChildRenderQueue(base_,world_,children,false))return Fail("fresh_child_render_queue");
    for(const auto* child:children) {
        const auto old=std::find_if(states_.begin(),states_.end(),[&](const auto& state) {
            return state.ref.state==child->historical.state && state.ref.controller==child->historical.controller;
        });
        unsigned visibility{};
        if(old==states_.end() || !RetainedField(values_,old->mesh.object,0x240,visibility)
            || !SetFreshChildVisibility(base_,child->state.mesh.object,visibility))return Fail("fresh_child_render_visibility");
    }
    if(!FreshChildRenderQueue(base_,world_,children,false))return Fail("fresh_child_visibility_render_queue");
    if(!RunFreshChildRenderWork(base_,world_))return Fail("fresh_child_render_native_work");
    if(!FreshChildRenderQueue(base_,world_,children,true))return Fail("fresh_child_render_unfinished");
    // This is CPU/native completion only. The host must obtain a newer ordered
    // GPU completion before projecting or releasing either B or fresh owners.
    return current.ValidateValues();
}
