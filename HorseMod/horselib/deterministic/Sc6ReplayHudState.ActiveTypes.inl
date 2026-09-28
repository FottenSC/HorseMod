// Independent-player reconstruction. These predicates validate consumed native
// inputs; they neither copy evaluation roots nor manufacture observations.
bool TypeReconstructionRejected(const char* reason) const {
    RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] HUD type reconstruction rejected check={}\n"),RC::to_generic_string(reason));
    return false;
}
bool TypePlayerBinding(const TypeEffect& e) const {
    constexpr unsigned char start_signature[]{0x48,0x8b,0xc4,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20,0x41,0x56,0x48,0x81,0xec};
    if(std::memcmp(reinterpret_cast<void*>(base_+0x1801aa0),start_signature,sizeof(start_signature)))return TypeReconstructionRejected("player_start_signature");
    constexpr unsigned char tick_signature[]{0x48,0x8b,0xc4,0x57,0x48,0x83,0xec,0x70,0x83,0xb9,0xd8,0x06,0,0,1,0x48,0x8b,0xf9,0x0f,0x85};
    if(std::memcmp(reinterpret_cast<void*>(base_+0x1826f90),tick_signature,sizeof(tick_signature)))return TypeReconstructionRejected("player_tick_signature");
    if(e.active_image.count!=1 || !Live(e.players[0]) || !Live(e.widget) || !Live(e.animation))return TypeReconstructionRejected("player_live_identity");
    auto* player=e.players[0].object;
    if(At<std::uintptr_t>(player,0)!=base_+0x373b998 || At<Object*>(player,0x370)!=e.animation.object
        || At<int>(player,0x378)!=e.widget.index || At<int>(player,0x37c)!=e.widget.serial
        || At<int>(player,0x380)!=e.animation.index || At<int>(player,0x384)!=e.animation.serial
        || At<void*>(player,0x398)!=reinterpret_cast<std::byte*>(e.animation.object)+0xb0
        || (At<unsigned char>(player,0x761)&1) || At<int>(player,0x770))return TypeReconstructionRejected("player_root_binding");
    // The native root's update multicast must be empty. Its private default
    // template-store binding must still resolve this exact animation asset.
    auto* store=At<void*>(player,0x468);
    if(!store || At<std::uintptr_t>(store,0)!=base_+0x36ec600 || At<unsigned char>(store,8)
        || At<int>(player,0x670) || At<int>(player,0x684))return TypeReconstructionRejected("player_template_store");
    if(At<int>(e.animation.object,0x338) || At<int>(e.animation.object,0x348))return TypeReconstructionRejected("player_asset_delegates");
    for(const auto& event:std::array<std::pair<const wchar_t*,std::uintptr_t>,2>{{
        {L"OnAnimationStarted",0x18a8760},{L"OnAnimationFinished",0x18a86d0}}}) {
        auto* function=e.widget.object->GetFunctionByNameInChain(event.first);
        if(!function || reinterpret_cast<std::uintptr_t>(function->GetFunc())!=base_+event.second
            || function->GetScript().Num())return TypeReconstructionRejected("player_widget_event");
    }
    auto* table=At<void*>(e.widget.object,0);
    if(At<std::uintptr_t>(table,0x290)!=base_+0x2d2bc0 || At<std::uintptr_t>(table,0x298)!=base_+0x2d2bc0)return TypeReconstructionRejected("player_widget_virtual");
    // One native weak-widget completion callback, no extra listener or active
    // traversal. Callback storage and its handle are preserved, never rebuilt.
    auto* collection=reinterpret_cast<std::byte*>(player)+0x6e0;
    if(At<int>(collection,0x50)!=1 || At<int>(collection,0x54)<1
        || At<int>(collection,0x54)>4 || At<int>(collection,0x64))return TypeReconstructionRejected("player_completion_collection");
    auto* entry=At<void*>(collection,0x40);if(!entry)entry=collection;
    if(At<int>(entry,0x30)!=3)return TypeReconstructionRejected("player_completion_entry");
    auto* callback=At<void*>(entry,0x20);if(!callback)return TypeReconstructionRejected("player_completion_storage");
    // Native142EC8E0C stores the handle at+0x28; vtable getter141F54830 reads it.
    const bool callback_valid=At<std::uintptr_t>(callback,0)==base_+0x3285198
        && At<int>(callback,8)==e.widget.index && At<int>(callback,12)==e.widget.serial
        && At<std::uintptr_t>(callback,16)==base_+0x17f7df0 && At<int>(callback,24)==0
        && At<std::uint64_t>(callback,0x28)!=0;
    if(!callback_valid)RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] HUD completion witness player={} callback={} vtable_rva={:x} weak_index={}/{} weak_serial={}/{} function_rva={:x} adjust={} handle={:x}\n"),
        e.players[0].index,reinterpret_cast<std::uintptr_t>(callback),At<std::uintptr_t>(callback,0)-base_,
        At<int>(callback,8),e.widget.index,At<int>(callback,12),e.widget.serial,
        At<std::uintptr_t>(callback,16)-base_,At<int>(callback,24),At<std::uint64_t>(callback,0x28));
    return callback_valid || TypeReconstructionRejected("player_completion_binding");
}
bool CaptureTypeSources(TypeEffect& e,std::vector<void*>& owners) const {
    const auto rows=At<Array>(e.animation.object,0xb0);
    if(!ValidArray(rows,32) || rows.count!=13 || At<int>(e.animation.object,0xe4))return false;
    unsigned ids{};
    for(int i=0;i<rows.count;++i) {
        auto* row=reinterpret_cast<std::byte*>(rows.data)+i*0x90;
        const auto id=At<unsigned>(row,0);
        if(id>=13 || (ids&(1u<<id)))return false;ids|=1u<<id;
        const auto children=At<Array>(row,8+0x28);
        if(!ValidArray(children,4) || children.count!=1)return false;
        auto* wrapper=reinterpret_cast<std::byte*>(children.data);
        if(!(At<unsigned char>(wrapper,0x30)&1))return false;
        auto* child=(At<unsigned char>(wrapper,0x30)&2)?wrapper:At<std::byte*>(wrapper,0);
        if(!child)return false;
        const auto type=At<std::uintptr_t>(child,0);
        if(type!=base_+0x3716800 && type!=base_+0x373a3f0 && type!=base_+0x3719640)return false;
        auto* section=At<Object*>(child,0x18);
        if(!ValidObject(section))return false;
        auto* track=At<Object*>(section,0x20);
        if(!ValidObject(track))return false;
        auto* movie=At<Object*>(track,0x20);
        if(!ValidObject(movie) || movie->GetClassPrivate()->GetName()!=L"MovieScene")return false;
        auto* animation=At<Object*>(movie,0x20);
        if(!ValidObject(animation) || animation->GetClassPrivate()->GetName()!=L"WidgetAnimation")return false;
        if(!e.source_animation.object) {
            // Live diagnostic da0ed195 identifies this exact cooked owner for
            // transient instances. No arbitrary foreign source is admitted.
            if(animation!=e.animation.object && animation!=RC::Unreal::UObjectGlobals::StaticFindObject<Object*>(nullptr,nullptr,
                STR("/Game/HUD/Cockpit/Widgets/DmgTypeEff.WidgetArchetype:DamgeEffect_INST")))return false;
            e.source_movie=Id(movie);e.source_animation=Id(animation);
        }
        if(e.source_movie.object!=movie || e.source_animation.object!=animation)return false;
        e.source_sections[id]=Id(section);e.source_tracks[id]=Id(track);
        if(!e.source_sections[id].object || !e.source_tracks[id].object)return false;
    }
    if(!e.source_movie.object || !e.source_animation.object)return false;
    for(unsigned i=0;i<13;++i){owners.push_back(e.source_sections[i].object);owners.push_back(e.source_tracks[i].object);}
    owners.push_back(e.source_movie.object);owners.push_back(e.source_animation.object);
    return true;
}
bool TypeAssetReconstructible(const TypeEffect& e) const {
    using RC::Unreal::FName;
    const FName none(L"None");
    auto* bindings=e.animation.object->GetValuePtrByPropertyNameInChain<Array>(L"AnimationBindings");
    auto** movie=e.animation.object->GetValuePtrByPropertyNameInChain<Object*>(L"MovieScene");
    if(!bindings || !ValidArray(*bindings,16) || bindings->count!=7 || !movie || !ValidObject(*movie)
        || (*movie)->GetClassPrivate()->GetName()!=L"MovieScene")return TypeReconstructionRejected("animation_bindings");
    const auto start=At<float>(*movie,0x10c),finish=At<float>(*movie,0x114);
    if(!e.playback[0].matches_asset_range(start,finish)
        || At<float>(e.players[0].object,0x694)!=start || At<float>(e.players[0].object,0x69c)!=finish
        || At<double>(e.players[0].object,0x6b0)!=e.playback[0].offset)
        return TypeReconstructionRejected("authored_playback_range");
    std::array<std::array<unsigned,4>,7> guids{};unsigned names{};
    for(int i=0;i<bindings->count;++i) {
        // Registration1418D5930 / bool setter141A23BB0: 0x28-byte rows.
        auto* row=reinterpret_cast<std::byte*>(bindings->data)+i*0x28;
        if(!At<FName>(row,8).Equals(none) || At<unsigned char>(row,0x20))return TypeReconstructionRejected("slot_or_root_binding");
        unsigned n{};for(;n<7;++n)if(At<FName>(row,0).Equals(FName(n==6?L"MAIN":type_images_[n])))break;
        if(n==7 || (names&(1u<<n)))return TypeReconstructionRejected("binding_widget_name");
        names|=1u<<n;std::memcpy(guids[n].data(),row+0x10,16);
        for(unsigned k=0;k<7;++k)if(k!=n && (names&(1u<<k)) && guids[k]==guids[n])return TypeReconstructionRejected("binding_guid_alias");
    }
    // Native141673EB0: main compiled-track sparse rows have stride0x90 and
    // value+8. Reject holes/stale tracks rather than following unknown chains.
    auto* root=reinterpret_cast<std::byte*>(e.animation.object)+0xb0;
    const auto tracks=At<Array>(root,0);
    if(!ValidArray(tracks,32) || tracks.count!=13 || At<int>(root,0x34)
        || At<int>(root,0x58) || At<int>(root,0xd8) || (At<unsigned char>(root,0x218)&2))return TypeReconstructionRejected("compiled_track_inventory");
    unsigned identifiers{};std::array<unsigned,7> properties{};
    for(int i=0;i<tracks.count;++i) {
        auto* row=reinterpret_cast<std::byte*>(tracks.data)+i*0x90;
        const auto id=At<unsigned>(row,0);
        if(id>=13 || (identifiers&(1u<<id)))return TypeReconstructionRejected("compiled_track_identifier");
        identifiers|=1u<<id;auto* track=row+8;
        unsigned binding{};for(;binding<7;++binding)if(!std::memcmp(track,guids[binding].data(),16))break;
        if(binding==7 || (At<unsigned char>(track,0x68)&1))return TypeReconstructionRejected("compiled_track_binding_or_override");
        const auto children=At<Array>(track,0x28);
        if(!ValidArray(children,4) || children.count!=1)return TypeReconstructionRejected("compiled_child_count");
        auto* wrapper=reinterpret_cast<std::byte*>(children.data);
        if(!(At<unsigned char>(wrapper,0x30)&1))return TypeReconstructionRejected("compiled_child_empty");
        auto* child=(At<unsigned char>(wrapper,0x30)&2)?wrapper:At<std::byte*>(wrapper,0);
        if(!child)return TypeReconstructionRejected("compiled_child_storage");
        const auto type=At<std::uintptr_t>(child,0);
        unsigned property{};const wchar_t* name{};const wchar_t* section_name{};
        if(type==base_+0x3716800){property=1;name=L"ColorAndOpacity";section_name=L"MovieSceneColorSection";}
        else if(type==base_+0x373a3f0){property=2;name=L"RenderTransform";section_name=L"MovieScene2DTransformSection";}
        else if(type==base_+0x3719640){property=4;name=L"Visibility";section_name=L"MovieSceneByteSection";}
        else return TypeReconstructionRejected("compiled_child_type");
        if((binding==6)!=(property==4) || (properties[binding]&property))return TypeReconstructionRejected("compiled_property_owner");
        properties[binding]|=property;
        if(At<unsigned char>(child,0x10)!=0 || !At<FName>(child,0x20).Equals(FName(name))
            || !At<FName>(child,0x38).Equals(none) || !At<FName>(child,0x40).Equals(none))return TypeReconstructionRejected("completion_or_property_callback");
        const auto path=At<Array>(child,0x28);const auto length=std::wcslen(name)+1;
        if(!ValidArray(path,32) || path.count!=length || std::memcmp(path.data,name,length*sizeof(wchar_t)))return TypeReconstructionRejected("compiled_property_path");
        if((property==1 && At<unsigned char>(child,0x208)!=1)
            || (property==2 && At<unsigned char>(child,0x358)!=1))return TypeReconstructionRejected("nonabsolute_blend");
        auto* section=At<Object*>(child,0x18);
        if(!ValidObject(section) || section->GetClassPrivate()->GetName()!=section_name)return TypeReconstructionRejected("source_section");
        auto* owner=At<Object*>(section,0x20);
        if(!e.source_sections[id].object || !e.source_tracks[id].object || !e.source_movie.object || !e.source_animation.object
            || !Live(e.source_sections[id]) || !Live(e.source_tracks[id]) || !Live(e.source_movie) || !Live(e.source_animation)
            || section!=e.source_sections[id].object || owner!=e.source_tracks[id].object
            || At<Object*>(owner,0x20)!=e.source_movie.object || At<Object*>(e.source_movie.object,0x20)!=e.source_animation.object)
            return TypeReconstructionRejected("retained_source_section_owner");
        auto** source_movie=e.source_animation.object->GetValuePtrByPropertyNameInChain<Object*>(L"MovieScene");
        if(!source_movie || *source_movie!=e.source_movie.object)return TypeReconstructionRejected("source_animation_movie");
        // Zero effective easing avoids either native or Blueprint easing
        // callbacks. No historical easing object or registry is installed.
        const auto ease_in=At<float>(section,At<unsigned char>(section,0xd0)?0xd4:0xb8);
        const auto ease_out=At<float>(section,At<unsigned char>(section,0xe8)?0xec:0xbc);
        if(ease_in!=0 || ease_out!=0)return TypeReconstructionRejected("active_easing");
    }
    for(unsigned n=0;n<7;++n)if(properties[n]!=(n==6?4u:3u))return TypeReconstructionRejected("missing_compiled_property");
    return true;
}

Status PrepareTypePlayers(const Sc6ReplayHudState& b,Prepared& prepared) const {
    if(prepared.target || !SameTypes(b))return Fail();
    for(int i=0;i<type_pool_image_.count;++i) {
        const auto& e=type_effects_[i];if(!e.active_image.count)continue;
        if(!e.reconstructible || !TypePlayerBinding(e) || !TypeAssetReconstructible(e))return Fail();
        if(e.stopped->count || !ValidArray(*e.active,4) || e.active->capacity>16
            || ((e.active->capacity==0)!=(e.active->data==nullptr))) {
            TypeReconstructionRejected("detached_player_backing");return Fail();
        }
        // Replacement checkpoints can share A's historical player with B.
        // It supplies only validated authored inputs/scalars; publication
        // constructs a different native player after B membership is parked.
        auto& owner=prepared.type_players[i];
        if(owner.lease || owner.phase!=Prepared::DamagePlayer::Phase::Empty)return Fail();
        owner.lease=std::make_shared<Sc6ReplayObjectLease>();
    }
    prepared.target=this;
    for(int i=0;i<type_pool_image_.count;++i)if(type_effects_[i].active_image.count && !prepared.players[i].Prepare())return Fail();
    return Status::success();
}
bool FinishReconstructedTypePlayer(const TypeEffect& e) const {
    if(!TypePlayerBinding(e) || !TypeAssetReconstructible(e))return false;
    if(At<int>(e.players[0].object,0x6d8)==0 && At<int>(e.players[0].object,0x400)==0
        && At<int>(e.players[0].object,0x410)==0)return true;
    reinterpret_cast<void(*)(void*)>(base_+0x18010d0)(e.players[0].object);
    // Native141674020 empties current evaluated tracks/entities before its
    // transition processing. Status alone does not prove that work completed.
    return TypePlayerBinding(e) && At<int>(e.players[0].object,0x6d8)==0
        && At<int>(e.players[0].object,0x400)==0 && At<int>(e.players[0].object,0x410)==0;
}
Status PublishTypePlayers(Prepared& prepared) const {
    if(prepared.target!=this || !prepared.private_hud)return Fail();
    constexpr unsigned char signature[]{0x40,0x56,0x57,0x48,0x83,0xec,0x68,0x48,0x8b,0xf2};
    if(std::memcmp(reinterpret_cast<void*>(base_+0x17ed0a0),signature,sizeof(signature)))return Fail();
    using Phase=Prepared::DamagePlayer::Phase;
    for(int i=0;i<type_pool_image_.count;++i) {
        const auto& e=type_effects_[i];if(!e.active_image.count)continue;
        if(e.active->count || e.stopped->count || !TypePlayerBinding(e) || !TypeAssetReconstructible(e)
            || !PublishTypeBacking(prepared,i))return Fail();
        const auto& saved=e.playback[0];auto& owner=prepared.type_players[i];
        if(owner.phase!=Phase::Empty || !owner.lease)return Fail();
        if(!prepared.players[i].Publish([]{return true;},[&] {
            // No old evaluation root is reset. The native constructor binds
            // an independent root and its required weak-widget callback.
            owner.phase=Phase::Constructing;
            auto* player=reinterpret_cast<Object*(*)(Object*,Object*)>(base_+0x17ed0a0)(e.widget.object,e.animation.object);
            owner.identity=Id(player);if(!owner.identity.object)return false;
            owner.phase=Phase::Constructed;
            if(e.active->count!=1 || e.active->data[0]!=player || player==e.players[0].object)return false;
            for(const auto& b:prepared.private_hud->type_effects_)for(const auto& id:b.players)if(id.object==player)return false;
            for(const auto& b:prepared.private_hud->effects_)for(const auto& id:b.players)if(id.object==player)return false;
            void* object=player;if(!owner.lease->Acquire(base_,{&object,1},1u<<20).ok())return false;
            owner.phase=Phase::Starting;
            reinterpret_cast<void(*)(void*,double,double,double,double,int,int,float)>(base_+0x1801aa0)(
                player,saved.time,saved.end,saved.lower,saved.upper,saved.loops,saved.mode,saved.rate);
            // Inner Start evaluates unshifted time. Native Tick adds the
            // authored offset; zero delta reconstructs that pose without
            // advancing the first-loop clock or synthesizing completion.
            reinterpret_cast<void(*)(void*,float)>(base_+0x1826f90)(player,0.0f);
            auto fresh=e;fresh.players[0]=owner.identity;
            ReplayHudPlayback observed;
            if(!TypePlayerBinding(fresh) || !TypeAssetReconstructible(fresh) || !ReplayHudPlayback::Read(
                {reinterpret_cast<const std::byte*>(player),0x780},observed) || !observed.same_values(saved))return false;
            owner.phase=Phase::Published;return true;
        }))return Fail();
        reinterpret_cast<void(*)(void*)>(base_+0x17f3f30)(e.widget.object);
    }
    return ValidateTypeValues(&prepared);
}
Status UndoTypePlayers(const Sc6ReplayHudState& physical,Prepared& prepared) const {
    if(!prepared.target || prepared.private_hud!=this)return Fail();
    const auto& a=*prepared.target;
    using Phase=Prepared::DamagePlayer::Phase;
    for(int i=0;i<a.type_pool_image_.count;++i) {
        auto& owner=prepared.type_players[i];const auto& e=type_effects_[i];
        if(owner.phase==Phase::Released)continue;
        if(owner.phase==Phase::Constructing)return Fail(); // unknown native result cannot be discarded
        if(e.private_player.phase()!=ReplayHudPrivatePlayer::Phase::Private && owner.phase==Phase::Empty) {
            if(!prepared.players[i].Undo([]{return true;}))return Fail();
            continue;
        }
        if(!ValidArray(*e.active,4) || e.active->capacity>16 || e.stopped->count)return Fail();
        // Prove every current member before the first native Finish. Never
        // touch B or an unobserved C birth merely because publication failed.
        for(int j=0;j<e.active->count;++j) {
            auto* player=e.active->data[j];
            for(const auto& b:type_effects_)for(const auto& id:b.players)if(player && player==id.object)return Fail();
            for(const auto& b:effects_)for(const auto& id:b.players)if(player && player==id.object)return Fail();
            bool known=player && player==owner.identity.object;
            if(this!=&physical)for(int k=0;k<physical.type_effects_[i].active_image.count;++k)
                known|=player==physical.type_effects_[i].players[k].object;
            if(!known)return Fail();
        }
        for(int j=0;j<e.active->count;++j) {
            const auto id=Id(e.active->data[j]);if(!id.object)return Fail();
            bool finished{};
            if(this!=&physical)for(int k=0;k<physical.type_effects_[i].active_image.count;++k)
                if(physical.type_effects_[i].players[k]==id
                    && physical.type_effects_[i].retirement[k]==TypeEffect::Retirement::Finished)finished=true;
            if(!finished) {auto fresh=a.type_effects_[i];fresh.players[0]=id;
                if(!a.FinishReconstructedTypePlayer(fresh))return Fail();}
        }
        if(owner.phase!=Phase::Empty && owner.phase!=Phase::Finished) {
            if(!Live(owner.identity))return Fail();
            owner.phase=Phase::Finishing;
            auto fresh=a.type_effects_[i];fresh.players[0]=owner.identity;
            if(!a.FinishReconstructedTypePlayer(fresh))return Fail();
            owner.phase=Phase::Finished;
        }
        e.active->count=0;
        if(!prepared.players[i].Undo([]{return true;}))return Fail();
    }
    return ReleaseTypePlayers(prepared,true)?Status::success():Fail();
}

bool ReleaseTypePlayers(Prepared& prepared,bool recovered=false) const {
    using Phase=Prepared::DamagePlayer::Phase;
    for(auto& owner:prepared.type_players) {
        if(owner.phase==Phase::Released)continue;
        if(owner.phase!=Phase::Empty && owner.phase!=(recovered?Phase::Finished:Phase::Published))return false;
        if(owner.lease && !owner.lease->Release().ok())return false;
        owner.lease.reset();owner.phase=Phase::Released;
    }
    return true;
}

// Native completion can shrink the active list to zero. Allocate only at
// publication, after complete B and native allocator signature admission.
// A B-active list is separately parked by ParkPrivateTypes and already has
// independent backing. This journal covers precisely an empty B allocation.
bool PublishTypeBacking(Prepared& prepared,int i) const {
    using Phase=Prepared::TypeBacking;
    if(prepared.target!=this || !prepared.private_hud || i<0 || i>=type_pool_image_.count)return false;
    const auto& e=type_effects_[i];auto& phase=prepared.type_backings[i];
    if(phase!=Phase::Empty || !ValidArray(*e.active,4) || e.active->count || e.stopped->count
        || e.active->capacity>16 || ((e.active->capacity==0)!=(e.active->data==nullptr)))return false;
    if(e.active->capacity)return true;
    const auto& b=prepared.private_hud->type_effects_[i];
    if(b.active_image.data || b.active_image.count || b.active_image.capacity)return false;
    phase=Phase::Allocating;
    reinterpret_cast<void(*)(void*,int)>(base_+0x1764ac0)(e.active,1);
    if(!e.active->data || e.active->capacity<1 || e.active->capacity>16 || e.active->count)return false;
    phase=Phase::Owned;return true;
}
bool RecoverTypeBackings(Prepared& prepared) const {
    using Phase=Prepared::TypeBacking;
    if(prepared.private_hud!=this)return false;
    for(int i=0;i<type_pool_image_.count;++i) {
        auto& phase=prepared.type_backings[i];if(phase==Phase::Empty || phase==Phase::Recovered)continue;
        const auto& e=type_effects_[i];
        if((phase!=Phase::Owned && phase!=Phase::Allocating) || e.active_image.data
            || e.active_image.count || e.active_image.capacity || !ValidArray(*e.active,4)
            || e.active->capacity>16 || e.stopped->count)return false;
        // Install validated every current member, finished discarded C and
        // retired the A player before this point. B had no allocation/player.
        phase=Phase::Recovering;e.active->count=0;
        reinterpret_cast<void(*)(void*,int)>(base_+0x1764ac0)(e.active,0);
        if(e.active->data || e.active->count || e.active->capacity)return false;
        phase=Phase::Recovered;
    }
    return true;
}
bool CommitTypeBackings(Prepared& prepared) const {
    using Phase=Prepared::TypeBacking;
    if(prepared.target!=this)return false;
    for(auto phase:prepared.type_backings)if(phase!=Phase::Empty && phase!=Phase::Owned && phase!=Phase::Adopted)return false;
    for(auto& phase:prepared.type_backings)if(phase==Phase::Owned)phase=Phase::Adopted;
    return true; // current backing belongs to the native widget, even if shrunk
}
