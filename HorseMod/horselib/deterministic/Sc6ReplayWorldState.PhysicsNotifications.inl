using NotificationBoundary=Sc6ReplayWorldState::PhysicsBoundary;
struct PhysicsNotificationRead {
    template<class T> bool operator()(std::uintptr_t address,T& value) const noexcept {
        __try {if(!address)return false;std::memcpy(&value,reinterpret_cast<void*>(address),sizeof(value));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
};
struct PhysicsNotificationWrite {
    template<class T> bool operator()(std::uintptr_t address,const T& value) const noexcept {
        __try {if(!address)return false;std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(value));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
};
bool PhysicsNotificationCoreOwned(const NotificationBoundary& image,unsigned scene,
    std::uintptr_t sc,std::uintptr_t core,bool live) noexcept {
    unsigned matches{};
    for(unsigned i=0;i<image.observed_actors[scene];++i) {
        const auto& row=image.actors[scene][i];
        if(!row.actor || row.actor+0x80!=core)continue;
        if(row.kind!=2 || !row.simulation
            || ReadAt<std::uintptr_t>(row.simulation_storage.data(),0)!=image.module+0x1aadc0
            || ReadAt<std::uintptr_t>(row.simulation_storage.data(),0x40)!=sc
            || ReadAt<std::uintptr_t>(row.simulation_storage.data(),0x48)!=core
            || ReadAt<std::uintptr_t>(row.dynamic_storage.data(),0x80)!=row.simulation)return false;
        if(live) {
            const PhysicsNotificationRead read{};std::uintptr_t sim{},owner{},bound{},vtable{};
            if(!read(core,sim) || sim!=row.simulation || !read(sim,vtable) || vtable!=image.module+0x1aadc0
                || !read(sim+0x40,owner) || owner!=sc || !read(sim+0x48,bound) || bound!=core)return false;
        }
        ++matches;
    }
    return matches==1;
}
bool PhysicsNotificationFlags(unsigned flags,unsigned membership) noexcept {
    return (flags&0x30)==membership && (flags&0xc0)!=0xc0
        && (!(flags&0x40) || (membership&0x10)) && (!(flags&0x80) || (membership&0x20));
}
bool PhysicsNotificationsOwned(const NotificationBoundary& image,unsigned scene,
    const ReplayPhysicsNotifications& notifications,bool live=false) noexcept {
    if(scene>=image.scenes.size() || image.observed_actors[scene]>image.actors[scene].size())return false;
    // The original empty-only representation cannot authorize pending work.
    // Its live installation also retains the empty-set admission guard.
    if(!notifications.valid) {
        for(unsigned i=0;i<image.observed_actors[scene];++i)
            if(ReadAt<unsigned short>(image.actors[scene][i].simulation_storage.data(),0xb4)&0xf0)return false;
        return !notifications.scene && notifications.Empty();
    }
    if(!notifications.WellFormed())return false;
    for(const auto& set:notifications.sets)for(unsigned i=0;i<set.count;++i)
        if(!PhysicsNotificationCoreOwned(image,scene,notifications.scene,set.cores[i],live))return false;
    const PhysicsNotificationRead read{};
    for(unsigned i=0;i<image.observed_actors[scene];++i) {
        const auto& row=image.actors[scene][i];if(row.kind!=2 || !row.simulation)continue;
        auto flags=ReadAt<unsigned short>(row.simulation_storage.data(),0xb4);
        if(live && !read(row.simulation+0xb4,flags))return false;
        if(!PhysicsNotificationFlags(flags,notifications.Membership(row.actor+0x80)))return false;
        if(flags&0xf0) {
            auto client=ReadAt<unsigned char>(row.dynamic_storage.data(),0x8b);
            auto actor_flags=ReadAt<unsigned char>(row.dynamic_storage.data(),0x8c);
            if(client>=notifications.client_count)return false;
            if(live) {
                unsigned char actual_client{},actual_flags{};
                if(!read(row.actor+0x8b,actual_client) || actual_client!=client
                    || !read(row.actor+0x8c,actual_flags) || actual_flags!=actor_flags)return false;
            }
        }
    }
    return true;
}
bool ReadPhysicsNotificationsForScene(const NotificationBoundary& owners,unsigned scene,
    std::uintptr_t sc,ReplayPhysicsNotifications& output) noexcept {
    return output.Capture(sc,PhysicsNotificationRead{},[&](std::uintptr_t core) {
        return PhysicsNotificationCoreOwned(owners,scene,sc,core,true);
    }) && PhysicsNotificationsOwned(owners,scene,output,true);
}
bool PhysicsNotificationsLiveMatch(const NotificationBoundary& expected,unsigned scene) noexcept {
    const auto& target=expected.notifications[scene];
    if(!target.valid)return PhysicsNotificationsOwned(expected,scene,target);
    ReplayPhysicsNotifications observed;
    return ReadPhysicsNotificationsForScene(expected,scene,target.scene,observed) && observed==target;
}
bool PhysicsNotificationPairValid(const NotificationBoundary& a,const NotificationBoundary& b,unsigned scene) noexcept {
    const auto& x=a.notifications[scene];const auto& y=b.notifications[scene];
    return x.valid==y.valid && PhysicsNotificationsOwned(a,scene,x) && PhysicsNotificationsOwned(b,scene,y)
        && (!x.valid || (x.scene==y.scene && x.client_storage==y.client_storage
            && x.client_count==y.client_count && x.clients==y.clients));
}
