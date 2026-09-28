// Native controller+3A0 owns an announcement factory, not widget instances.
// Its widgets are owned by the viewport. A excludes active announcements until
// their reconstruction is implemented; B remains attached and privately paused.
// See replay-announcement-ownership-2026-09-13.md for consumers and limitations.
Status CaptureAnnouncements(std::vector<void*>& owners) {
    constexpr unsigned char remove_signature[]{0x40,0x57,0x48,0x83,0xec,0x40,0x48,0x83,0xb9,0x08,0x02,0,0,0,0x48,0x8b,0xf9};
    if(std::memcmp(reinterpret_cast<void*>(base_+0x1814b60),remove_signature,sizeof(remove_signature)))return Fail();
    auto* actor=At<Object*>(controller_.object,0x3a0);
    if(!ValidObject(actor) || At<std::uintptr_t>(actor,0)!=base_+0x327db88
        || At<void*>(actor,0x98)!=battle_)return Fail();
    announce_actor_=Id(actor);if(!announce_actor_.object)return Fail();owners.push_back(actor);
    announce_enabled_=At<unsigned char>(actor,0x388);
    if(announce_enabled_>1 || At<std::uintptr_t>(reinterpret_cast<void*>(base_+0x327db88),0x608)!=base_+0x3fa280)return Fail();
    for(unsigned i=0;i<15;++i) {
        announce_classes_[i]=Id(At<Object*>(actor,0x390+8*i));
        if(!announce_classes_[i].object || !announce_classes_[i].object->IsA<RC::Unreal::UClass>())return Fail();
        owners.push_back(announce_classes_[i].object);
    }
    bool complete=true;
    RC::Unreal::UObjectGlobals::ForEachUObject([&](Object* widget,std::int32_t,std::int32_t) {
        using namespace RC::Unreal;
        if(!widget || widget->HasAnyFlags(static_cast<EObjectFlags>(RF_ClassDefaultObject|RF_ArchetypeObject)))return RC::LoopAction::Continue;
        // Class identity is immutable during this synchronous inventory.
        // Retain the full census and per-object lifetime/membership checks.
        const auto* widget_class=widget->GetClassPrivate();
        unsigned kind=15;
        for(unsigned i=0;i<15;++i)if(widget_class==announce_classes_[i].object) {kind=i;break;}
        // Resolve reflection inheritance only for a matching factory class.
        // The complete object census and all owner/lifetime checks remain.
        if(kind==15 || widget->IsA<UClass>() || !ValidObject(widget) || At<void*>(widget,0x20)!=At<void*>(world_,0x140)
            || replay_hud_widget_admission.Excludes(widget))return RC::LoopAction::Continue;
        auto* viewport=At<void*>(widget,0x208);auto* control=At<void*>(widget,0x210);
        if(!viewport || !control || At<int>(control,8)<1)return RC::LoopAction::Continue;
        // Both audited graphs play a widget-local animation, emit sound, set
        // child visibility and remove self on fadeout. Preserve native C as-is;
        // discarded C uses the same guarded Finish/removal path as private B.
        // This does not admit reconstruction of an active historical A widget.
        const auto name=widget->GetClassPrivate()->GetName();
        const bool audited=(kind==10 && name==L"BA_YouWin_C") || (kind==5 && name==L"BA_Great_C");
        if(!audited || announcement_count_==announcements_.size()) {
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] announcement admission unsupported class={} kind={}\n"),widget->GetClassPrivate()->GetName(),kind);
            complete=false;return RC::LoopAction::Break;
        }
        auto& e=announcements_[announcement_count_++];e.widget=Id(widget);e.klass=announce_classes_[kind];
        e.viewport=viewport;e.viewport_control=control;e.visibility=At<unsigned char>(widget,0x91);
        auto* table=At<void*>(widget,0);
        auto* active=widget->GetValuePtrByPropertyNameInChain<Array>(L"ActiveSequencePlayers");
        auto* stopped=widget->GetValuePtrByPropertyNameInChain<Array>(L"StoppedSequencePlayers");
        if(!e.widget.object || e.visibility>4 || active!=&At<Array>(widget,0x190) || stopped!=&At<Array>(widget,0x1a0)
            || !ValidArray(*active,4) || !ValidArray(*stopped,4) || stopped->count
            || At<std::uintptr_t>(table,0x248)!=base_+0x1814b60
            || At<std::uintptr_t>(table,0x2c8)!=base_+0x3f2d10 || (At<unsigned char>(widget,0x1d0)&1)) {
            complete=false;return RC::LoopAction::Break;
        }
        e.active=*active;e.stopped=*stopped;
        std::memcpy(e.clock.data(),&At<std::byte>(widget,0x230),e.clock.size());
        owners.push_back(widget);owners.push_back(e.klass.object);
        for(int j=0;j<active->count;++j) {
            auto* player=active->data[j];e.players[j]=Id(player);
            if(!e.players[j].object || At<std::uintptr_t>(player,0)!=base_+0x373b998
                || At<int>(player,0x378)!=e.widget.index || At<int>(player,0x37c)!=e.widget.serial
                || (At<unsigned char>(player,0x761)&1) || At<int>(player,0x770)) {complete=false;return RC::LoopAction::Break;}
            e.animations[j]=Id(At<Object*>(player,0x370));
            if(!e.animations[j].object || e.animations[j].object->GetClassPrivate()->GetName()!=L"WidgetAnimation") {complete=false;return RC::LoopAction::Break;}
            std::memcpy(e.witnesses[j].data(),player,0x780);
            owners.push_back(player);owners.push_back(e.animations[j].object);
        }
        return RC::LoopAction::Continue;
    });
    if(!complete)return Fail();
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] announcement capture count={} private_excluded=true\n"),announcement_count_);
    return Status::success();
}
Status ValidateAnnouncements() const {
    if(!Live(announce_actor_) || At<Object*>(controller_.object,0x3a0)!=announce_actor_.object
        || At<std::uintptr_t>(announce_actor_.object,0)!=base_+0x327db88 || At<void*>(announce_actor_.object,0x98)!=battle_)return Fail();
    for(unsigned i=0;i<15;++i)if(!Live(announce_classes_[i]) || At<Object*>(announce_actor_.object,0x390+8*i)!=announce_classes_[i].object)return Fail();
    for(unsigned i=0;i<announcement_count_;++i) {
        const auto& e=announcements_[i];
        if(!Live(e.widget) || !Live(e.klass) || e.widget.object->GetClassPrivate()!=e.klass.object)return Fail();
        if(e.life==Announcement::Life::Removed)continue;
        auto* widget=e.widget.object;
        if(e.life==Announcement::Life::Removing) {
            if(At<Array>(widget,0x190).count || At<Array>(widget,0x1a0).count)return Fail();
            continue;
        }
        if(At<void*>(widget,0x208)!=e.viewport || At<void*>(widget,0x210)!=e.viewport_control || At<int>(e.viewport_control,8)<1
            || std::memcmp(&At<Array>(widget,0x190),&e.active,sizeof(Array))
            || std::memcmp(&At<Array>(widget,0x1a0),&e.stopped,sizeof(Array)))return Fail();
        for(int j=0;j<e.active.count;++j) {
            if(!Live(e.players[j]) || !Live(e.animations[j]) || e.active.data[j]!=e.players[j].object)return Fail();
            if(e.life!=Announcement::Life::Finishing && std::memcmp(e.players[j].object,e.witnesses[j].data(),0x780))return Fail();
        }
        if(e.life!=Announcement::Life::Finishing && std::memcmp(&At<std::byte>(widget,0x230),e.clock.data(),e.clock.size()))return Fail();
        if(e.life==Announcement::Life::Attached && At<unsigned char>(widget,0x91)!=e.visibility)return Fail();
        if(e.life==Announcement::Life::Private && (!replay_hud_widget_admission.OwnedBy(this)
            || !replay_hud_widget_admission.Excludes(widget) || At<unsigned char>(widget,0x91)!=1))return Fail();
    }
    return Status::success();
}
Status ParkAnnouncements() const {
    if(!announcement_count_)return Status::success();
    if(!NativeReplayWidgetClock::Owns(world_,thread_) || !ValidateAnnouncements().ok())return Fail();
    std::array<const void*,16> widgets{};
    for(unsigned i=0;i<announcement_count_;++i)widgets[i]=announcements_[i].widget.object;
    if(!replay_hud_widget_admission.Acquire(this,{widgets.data(),announcement_count_}))return Fail();
    for(unsigned i=0;i<announcement_count_;++i) {
        const auto& e=announcements_[i];
        if(e.life!=Announcement::Life::Attached && e.life!=Announcement::Life::Private)return Fail();
        e.life=Announcement::Life::Private;
        reinterpret_cast<void(*)(void*,unsigned char)>(base_+0x181e1b0)(e.widget.object,1);
    }
    return ValidateAnnouncements();
}
Status RecoverAnnouncements() const {
    if(!ValidateAnnouncements().ok())return Fail();
    for(unsigned i=0;i<announcement_count_;++i) {
        const auto& e=announcements_[i];
        if(e.life!=Announcement::Life::Attached && e.life!=Announcement::Life::Private)return Fail();
        reinterpret_cast<void(*)(void*,unsigned char)>(base_+0x181e1b0)(e.widget.object,e.visibility);
        if(At<unsigned char>(e.widget.object,0x91)!=e.visibility)return Fail();
        e.life=Announcement::Life::Attached;
    }
    if(replay_hud_widget_admission.OwnedBy(this) && !replay_hud_widget_admission.Release(this))return Fail();
    return Status::success();
}
Status RetireAnnouncements(const Sc6ReplayHudState* preserved=nullptr) const {
    if(this==preserved)return Status::success();
    if(!ValidateAnnouncements().ok())return Fail();
    for(unsigned i=0;i<announcement_count_;++i) {
        const auto& e=announcements_[i];bool keep=false;
        if(preserved)for(unsigned j=0;j<preserved->announcement_count_;++j)keep|=e.widget==preserved->announcements_[j].widget;
        if(keep || e.life==Announcement::Life::Removed)continue;
        if(e.life!=Announcement::Life::Removing)e.life=Announcement::Life::Finishing;
        for(;e.finish_cursor<unsigned(e.active.count);++e.finish_cursor) {
            auto* player=e.players[e.finish_cursor].object;
            if(!Live(e.players[e.finish_cursor]) || At<std::uintptr_t>(player,0)!=base_+0x373b998
                || (At<unsigned char>(player,0x761)&1) || At<int>(player,0x770))return Fail();
            reinterpret_cast<void(*)(void*)>(base_+0x18010d0)(player);
            if(At<int>(player,0x6d8) || (At<unsigned char>(player,0x761)&1) || At<int>(player,0x770))return Fail();
        }
        // No Stop/Play or OnAnimationFinished dispatch. Finish closes native
        // evaluation roots; removal releases current viewport ownership. Leases
        // remain until the host's existing ordered GPU retirement completes.
        At<Array>(e.widget.object,0x190).count=0;
        e.life=Announcement::Life::Removing;
        reinterpret_cast<void(*)(void*)>(base_+0x1814b60)(e.widget.object);
        // Native1417F43D0 uses this exact weak-owner predicate. A surviving
        // viewport owner is pending removal, never successful retirement.
        auto* control=At<void*>(e.widget.object,0x210);
        if(At<void*>(e.widget.object,0x208) && control && At<int>(control,8)>0)return Fail();
        e.life=Announcement::Life::Removed;
    }
    return Status::success();
}
Status CommitAnnouncements() const {
    for(unsigned i=0;i<announcement_count_;++i) {
        const auto life=announcements_[i].life;
        if(life==Announcement::Life::Attached
            || (life!=Announcement::Life::Removed && !replay_hud_widget_admission.OwnedBy(this)))return Fail();
    }
    if(!RetireAnnouncements().ok())return Fail();
    if(replay_hud_widget_admission.OwnedBy(this) && !replay_hud_widget_admission.Release(this))return Fail();
    return Status::success();
}
