// DmgValueEff fall_out: one image color and one canvas-slot margin track.
// Cooked input: initial-hud-scripts/DmgValueEff.script.json. Native player
// construction owns its callback and evaluation storage; B is never restarted.
bool DamagePlayerBinding(const Effect& e,const ObjectId& player) const {
    TypeEffect binding{};binding.widget=e.widget;binding.animation=e.animation;
    binding.active_image.count=1;binding.players[0]=player;
    return TypePlayerBinding(binding);
}
bool CaptureDamageSources(Effect& e,std::vector<void*>& owners) const {
    const auto rows=At<Array>(e.animation.object,0xb0);
    if(!ValidArray(rows,2) || rows.count!=2 || At<int>(e.animation.object,0xe4))return false;
    unsigned ids{};
    for(int i=0;i<2;++i) {
        auto* row=reinterpret_cast<std::byte*>(rows.data)+i*0x90;
        const auto id=At<unsigned>(row,0);if(id>=2 || (ids&(1u<<id)))return false;ids|=1u<<id;
        const auto children=At<Array>(row,0x30);
        if(!ValidArray(children,1) || children.count!=1)return false;
        auto* wrapper=reinterpret_cast<std::byte*>(children.data);
        if(!(At<unsigned char>(wrapper,0x30)&1))return false;
        auto* child=(At<unsigned char>(wrapper,0x30)&2)?wrapper:At<std::byte*>(wrapper,0);
        if(!child || (At<std::uintptr_t>(child,0)!=base_+0x3716800 && At<std::uintptr_t>(child,0)!=base_+0x373a9d8))return false;
        auto* section=At<Object*>(child,0x18);if(!ValidObject(section))return false;
        auto* track=At<Object*>(section,0x20);if(!ValidObject(track))return false;
        auto* movie=At<Object*>(track,0x20);if(!ValidObject(movie) || movie->GetClassPrivate()->GetName()!=L"MovieScene")return false;
        auto* animation=At<Object*>(movie,0x20);if(!ValidObject(animation) || animation->GetClassPrivate()->GetName()!=L"WidgetAnimation")return false;
        if(!e.source_animation.object) {
            if(animation!=e.animation.object && animation!=RC::Unreal::UObjectGlobals::StaticFindObject<Object*>(nullptr,nullptr,
                STR("/Game/HUD/Cockpit/Widgets/DmgValueEff.WidgetArchetype:fall_out_INST")))return false;
            e.source_movie=Id(movie);e.source_animation=Id(animation);
        }
        if(e.source_movie.object!=movie || e.source_animation.object!=animation)return false;
        e.source_sections[id]=Id(section);e.source_tracks[id]=Id(track);
        if(!e.source_sections[id].object || !e.source_tracks[id].object)return false;
    }
    for(unsigned i=0;i<2;++i){owners.push_back(e.source_sections[i].object);owners.push_back(e.source_tracks[i].object);}
    owners.push_back(e.source_movie.object);owners.push_back(e.source_animation.object);return true;
}
bool DamageAssetReconstructible(const Effect& e) const {
    using RC::Unreal::FName;
    const auto reject=[&](const char* reason){return TypeReconstructionRejected(reason);};
    if(!Live(e.animation) || !Live(e.slot) || !Live(e.image) || !Live(e.widget)
        || e.widget.object->GetClassPrivate()->GetName()!=L"DmgValueEff_C")return false;
    auto* bindings=e.animation.object->GetValuePtrByPropertyNameInChain<Array>(L"AnimationBindings");
    auto** movie=e.animation.object->GetValuePtrByPropertyNameInChain<Object*>(L"MovieScene");
    if(!bindings || !ValidArray(*bindings,2) || bindings->count!=2 || !movie || !ValidObject(*movie)
        || (*movie)->GetClassPrivate()->GetName()!=L"MovieScene")return reject("damage_bindings");
    if(!e.playback[0].matches_asset_range(At<float>(*movie,0x10c),At<float>(*movie,0x114)))return reject("damage_range");
    std::array<std::array<unsigned,4>,2> guids{};unsigned names{};
    for(int i=0;i<2;++i) {
        auto* row=reinterpret_cast<std::byte*>(bindings->data)+i*0x28;
        if(!At<FName>(row,0).Equals(FName(L"damage")) || At<unsigned char>(row,0x20))return reject("damage_binding_owner");
        unsigned n=At<FName>(row,8).Equals(FName(L"None"))?0:1;
        if((n && !At<FName>(row,8).Equals(FName(L"CanvasPanelSlot_1"))) || (names&(1u<<n)))return reject("damage_slot_binding");
        names|=1u<<n;std::memcpy(guids[n].data(),row+0x10,16);
    }
    if(guids[0]==guids[1])return false;
    auto* root=reinterpret_cast<std::byte*>(e.animation.object)+0xb0;
    const auto rows=At<Array>(root,0);
    if(!ValidArray(rows,2) || rows.count!=2 || At<int>(root,0x34) || At<int>(root,0x58)
        || At<int>(root,0xd8) || (At<unsigned char>(root,0x218)&2))return reject("damage_compiled_inventory");
    unsigned ids{},properties{};
    for(int i=0;i<2;++i) {
        auto* row=reinterpret_cast<std::byte*>(rows.data)+i*0x90;
        const auto id=At<unsigned>(row,0);if(id>=2 || (ids&(1u<<id)))return false;ids|=1u<<id;
        auto* track=row+8;unsigned n{};
        for(;n<2;++n)if(!std::memcmp(track,guids[n].data(),16))break;
        if(n==2 || (properties&(1u<<n)) || (At<unsigned char>(track,0x68)&1))return false;properties|=1u<<n;
        const auto children=At<Array>(track,0x28);if(!ValidArray(children,1) || children.count!=1)return false;
        auto* wrapper=reinterpret_cast<std::byte*>(children.data);if(!(At<unsigned char>(wrapper,0x30)&1))return false;
        auto* child=(At<unsigned char>(wrapper,0x30)&2)?wrapper:At<std::byte*>(wrapper,0);
        const auto* name=n?L"Offsets":L"ColorAndOpacity";
        const auto* path=n?L"LayoutData.Offsets":L"ColorAndOpacity";
        if(!child || At<std::uintptr_t>(child,0)!=base_+(n?0x373a9d8:0x3716800)
            || At<unsigned char>(child,0x10)!=0 || At<unsigned char>(child,0x208)!=1
            || !At<FName>(child,0x20).Equals(FName(name)) || !At<FName>(child,0x38).Equals(FName(L"None"))
            || !At<FName>(child,0x40).Equals(FName(L"None")))return reject("damage_property_inputs");
        const auto text=At<Array>(child,0x28);const auto length=std::wcslen(path)+1;
        if(!ValidArray(text,32) || text.count!=length || std::memcmp(text.data,path,length*sizeof(wchar_t)))return false;
        auto* section=At<Object*>(child,0x18);
        if(!e.source_sections[id].object || !e.source_tracks[id].object || !e.source_movie.object || !e.source_animation.object
            || !Live(e.source_sections[id]) || !Live(e.source_tracks[id]) || !Live(e.source_movie) || !Live(e.source_animation)
            || section!=e.source_sections[id].object || section->GetClassPrivate()->GetName()!=(n?L"MovieSceneMarginSection":L"MovieSceneColorSection")
            || At<Object*>(section,0x20)!=e.source_tracks[id].object
            || At<Object*>(e.source_tracks[id].object,0x20)!=e.source_movie.object
            || At<Object*>(e.source_movie.object,0x20)!=e.source_animation.object)return reject("damage_source_owner");
        if(At<float>(section,At<unsigned char>(section,0xd0)?0xd4:0xb8)!=0
            || At<float>(section,At<unsigned char>(section,0xe8)?0xec:0xbc)!=0)return reject("damage_easing");
    }
    return ids==3 && properties==3;
}
bool PublishDamagePlayers(Prepared& prepared) const {
    if(prepared.target!=this || !prepared.private_hud)return false;
    constexpr unsigned char signature[]{0x40,0x56,0x57,0x48,0x83,0xec,0x68,0x48,0x8b,0xf2};
    if(std::memcmp(reinterpret_cast<void*>(base_+0x17ed0a0),signature,sizeof(signature)))return false;
    using Phase=Prepared::DamagePlayer::Phase;
    for(int i=0;i<pool_image_.count;++i) {
        const auto& e=effects_[i];if(!e.active_image.count)continue;
        auto& operation=prepared.damage_players[i];
        if(operation.phase!=Phase::Empty || !operation.lease || e.active->count || e.stopped->count
            || !e.reconstructible || !DamageAssetReconstructible(e))return false;
        operation.phase=Phase::Constructing;
        auto* player=reinterpret_cast<Object*(*)(Object*,Object*)>(base_+0x17ed0a0)(e.widget.object,e.animation.object);
        operation.identity=Id(player);if(!operation.identity.object)return false;
        operation.phase=Phase::Constructed;
        if(e.active->count!=1 || e.active->data[0]!=player || player==e.players[0].object)return false;
        for(const auto& b:prepared.private_hud->effects_)for(const auto& id:b.players)if(id.object==player)return false;
        void* owner=player;if(!operation.lease->Acquire(base_,{&owner,1},1u<<20).ok())return false;
        // Constructor initializes the native animation and widget binding;
        // Start initializes the independent template root. Empty start delegates
        // were admitted from A; no logical Start callback is replayed twice.
        const auto& saved=e.playback[0];operation.phase=Phase::Starting;
        reinterpret_cast<void(*)(void*,double,double,double,double,int,int,float)>(base_+0x1801aa0)(
            player,saved.time,saved.end,saved.lower,saved.upper,saved.loops,saved.mode,saved.rate);
        reinterpret_cast<void(*)(void*,float)>(base_+0x1826f90)(player,0.0f);
        ReplayHudPlayback observed;
        if(!DamagePlayerBinding(e,operation.identity) || !ReplayHudPlayback::Read({reinterpret_cast<const std::byte*>(player),0x780},observed)
            || !observed.same_values(saved))return false;
        operation.phase=Phase::Published;
        reinterpret_cast<void(*)(void*)>(base_+0x17f3f30)(e.widget.object);
    }
    return prepared.private_hud->ValidateBindings(false).ok();
}
bool ValidateDiscardedDamage(const Sc6ReplayHudState& physical,const Prepared& prepared,int index) const {
    const auto& e=effects_[index];const auto& c=physical.effects_[index];
    if(!ValidArray(*e.active,4) || !ValidArray(*e.stopped,4) || e.active->capacity>16 || e.stopped->count)return false;
    for(int j=0;j<e.active->count;++j) {
        auto* player=e.active->data[j];
        for(const auto& b:effects_)for(const auto& id:b.players)if(player && player==id.object)return false;
        bool known=player==prepared.damage_players[index].identity.object;
        if(this!=&physical)for(int k=0;k<c.active_image.count;++k)known|=player==c.players[k].object;
        if(!known)return false;
    }
    return true;
}
bool UndoDamagePlayers(const Sc6ReplayHudState& physical,Prepared& prepared) const {
    if(prepared.private_hud!=this || !prepared.target)return false;
    using Phase=Prepared::DamagePlayer::Phase;
    for(int i=0;i<pool_image_.count;++i) {
        const auto& e=effects_[i];auto& operation=prepared.damage_players[i];
        if(operation.phase==Phase::Released)continue;
        if(operation.phase==Phase::Constructing)return false; // unknown native result stays contained
        if(e.damage_backing!=DamageBackingPhase::Private && operation.phase==Phase::Empty)continue;
        if(!ValidateDiscardedDamage(physical,prepared,i))return false;
        // Includes native C births after publication, as well as our fresh A.
        for(int j=0;j<e.active->count;++j) {
            Effect current{};current.animation=e.animation;current.active_image.count=1;current.players[0]=Id(e.active->data[j]);
            if(!current.players[0].object || !FinishDamagePlayer(current,0))return false;
        }
        if(operation.phase!=Phase::Empty && operation.phase!=Phase::Finished && operation.phase!=Phase::Released) {
            if(!Live(operation.identity))return false;
            operation.phase=Phase::Finishing;
            Effect current{};current.animation=e.animation;current.active_image.count=1;current.players[0]=operation.identity;
            if(!FinishDamagePlayer(current,0))return false;
            operation.phase=Phase::Finished;
        }
        e.active->count=0;
    }
    return ReleaseDamagePlayers(prepared,true);
}
bool ReleaseDamagePlayers(Prepared& prepared,bool recovered=false) const {
    using Phase=Prepared::DamagePlayer::Phase;
    for(auto& operation:prepared.damage_players) {
        if(operation.phase==Phase::Released)continue;
        if(operation.phase!=Phase::Empty && operation.phase!=(recovered?Phase::Finished:Phase::Published))return false;
        if(operation.lease && !operation.lease->Release().ok())return false;
        operation.lease.reset();operation.phase=Phase::Released;
    }
    return true;
}
