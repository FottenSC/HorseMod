// Cooked DmgTypeEff: six image color/transform tracks and MAIN visibility.
// CreateDamageTypeEffect reparents the pooled widget and writes its complete
// root transform before PlayAnimation. Inactive historical roots may keep the
// current native canvas slot; unused roots have zero area until that producer.
// No cached Slate pointer, owning player image, or historical slot is installed.
inline static constexpr const wchar_t* type_images_[]{L"6Light",L"6Light_g",L"Ring_g1",L"Ring1",L"Ring2",L"Ring_g2"};
bool TypesInactive() const noexcept {
    if(type_pool_image_.count<1 || type_pool_image_.count>16)return false;
    for(int i=0;i<type_pool_image_.count;++i)if(type_effects_[i].active_image.count)return false;
    return true;
}
bool TypeAttached(const TypeEffect& e,bool& attached) const {
    Effect root{};root.widget=e.widget;return Attached(root,attached);
}
std::array<unsigned,7> TypeTransform(const TypeEffect& e,bool attached) const {
    auto value=e.transform;
    if(e.unused && attached)value[2]=value[3]=0;
    return value;
}
Status CaptureTypes(std::vector<void*>& owners) {
    constexpr unsigned char finish_signature[]{0x40,0x53,0x48,0x83,0xec,0x20,0xf6,0x81,0x61,0x07,0x00,0x00,0x01,0x48,0x8b,0xd9};
    constexpr unsigned char visibility_signature[]{0x40,0x53,0x48,0x81,0xec,0x80,0x00,0x00,0x00,0x0f,0xb6,0xda,0x88,0x91,0x91,0x00};
    if(std::memcmp(reinterpret_cast<void*>(base_+0x18010d0),finish_signature,sizeof(finish_signature))
        || std::memcmp(reinterpret_cast<void*>(base_+0x181e1b0),visibility_signature,sizeof(visibility_signature)))return Fail();
    type_pool_=cockpit_.object->GetValuePtrByPropertyNameInChain<Array>(L"DmgTypeEff");
    if(!type_pool_ || !ValidArray(*type_pool_,16) || !type_pool_->count)return Fail();
    type_pool_image_=*type_pool_;
    for(int i=0;i<type_pool_image_.count;++i) {
        auto& e=type_effects_[i];e.widget=Id(type_pool_->data[i]);
        if(!e.widget.object || e.widget.object->GetClassPrivate()->GetName()!=L"DmgTypeEff_C")return Fail();
        auto** animation=e.widget.object->GetValuePtrByPropertyNameInChain<Object*>(L"DamgeEffect");
        auto** main=e.widget.object->GetValuePtrByPropertyNameInChain<Object*>(L"MAIN");
        if(!animation || !main)return Fail();e.animation=Id(*animation);e.main=Id(*main);
        if(!e.animation.object || !e.main.object || e.animation.object->GetClassPrivate()->GetName()!=L"WidgetAnimation"
            || e.main.object->GetClassPrivate()->GetName()!=L"CanvasPanel")return Fail();
        bool attached{};if(!TypeAttached(e,attached))return Fail();e.unused=!attached;
        if(e.widget.object->GetValuePtrByPropertyNameInChain<unsigned>(L"RenderTransform")!=&At<unsigned>(e.widget.object,0xb0)
            || e.main.object->GetValuePtrByPropertyNameInChain<unsigned char>(L"Visibility")!=&At<unsigned char>(e.main.object,0x91))return Fail();
        std::memcpy(e.transform.data(),&At<unsigned>(e.widget.object,0xb0),28);
        e.visibility=At<unsigned char>(e.main.object,0x91);if(e.visibility>4)return Fail();
        for(unsigned n=0;n<e.images.size();++n) {
            auto** image=e.widget.object->GetValuePtrByPropertyNameInChain<Object*>(type_images_[n]);
            if(!image)return Fail();auto& row=e.images[n];row.object=Id(*image);
            if(!row.object.object || row.object.object->GetClassPrivate()->GetName()!=L"Image"
                || row.object.object->GetValuePtrByPropertyNameInChain<unsigned>(L"ColorAndOpacity")!=&At<unsigned>(row.object.object,0x1c0)
                || row.object.object->GetValuePtrByPropertyNameInChain<unsigned>(L"RenderTransform")!=&At<unsigned>(row.object.object,0xb0))return Fail();
            std::memcpy(row.color.data(),&At<unsigned>(row.object.object,0x1c0),16);
            std::memcpy(row.transform.data(),&At<unsigned>(row.object.object,0xb0),28);owners.push_back(row.object.object);
        }
        e.active=e.widget.object->GetValuePtrByPropertyNameInChain<Array>(L"ActiveSequencePlayers");
        e.stopped=e.widget.object->GetValuePtrByPropertyNameInChain<Array>(L"StoppedSequencePlayers");
        if(!e.active || !e.stopped || !ValidArray(*e.active,4) || !ValidArray(*e.stopped,4) || e.stopped->count)return Fail();
        e.active_image=*e.active;e.stopped_image=*e.stopped;
        if(e.unused && e.active_image.count)return Fail();
        for(int j=0;j<e.active_image.count;++j) {
            e.players[j]=Id(e.active->data[j]);auto* player=e.players[j].object;
            if(!player || At<std::uintptr_t>(player,0)!=base_+0x373b998 || At<void*>(player,0x370)!=e.animation.object
                || (At<unsigned char>(player,0x761)&1) || At<int>(player,0x770))return Fail();
            if(!ReplayHudPlayback::Read({reinterpret_cast<const std::byte*>(player),0x780},e.playback[j]))return Fail();
            owners.push_back(player);
        }
        e.reconstructible=e.active_image.count==1 && e.playback[0].forward_first_loop()
            && TypePlayerBinding(e) && CaptureTypeSources(e,owners) && TypeAssetReconstructible(e);
        for(auto* o:{e.widget.object,e.animation.object,e.main.object})owners.push_back(o);
    }
    return Status::success();
}
bool SameTypes(const Sc6ReplayHudState& b) const {
    if(type_pool_!=b.type_pool_ || type_pool_image_.count!=b.type_pool_image_.count)return false;
    for(int i=0;i<type_pool_image_.count;++i) {
        const auto& a=type_effects_[i];const auto& c=b.type_effects_[i];
        if(a.widget!=c.widget || a.animation!=c.animation || a.main!=c.main)return false;
        for(unsigned n=0;n<a.images.size();++n)if(a.images[n].object!=c.images[n].object)return false;
    }
    return true;
}
static std::span<const std::byte> TypePlayerImage(const TypeEffect& e) {
    return {reinterpret_cast<const std::byte*>(e.players[0].object),0x780};
}
bool TypePlayerAbsent(Object* player) const {
    const auto absent=[&](const auto& e) {
        if(!e.active || !e.stopped || !ValidArray(*e.active,4) || !ValidArray(*e.stopped,4))return false;
        for(int j=0;j<e.active->count;++j)if(e.active->data[j]==player)return false;
        for(int j=0;j<e.stopped->count;++j)if(e.stopped->data[j]==player)return false;
        return true;
    };
    for(int i=0;i<pool_image_.count;++i)if(!absent(effects_[i]))return false;
    for(int i=0;i<type_pool_image_.count;++i)if(!absent(type_effects_[i]))return false;
    return true;
}
Status PreparePrivateTypes() const {
    constexpr unsigned char resize_signature[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x63,0xda,0x48,0x8b,0xf9,0x85,0xd2,0x74,0x1e,0x48,0x8b,0xcb,0x33};
    if(std::memcmp(reinterpret_cast<void*>(base_+0x1764ac0),resize_signature,sizeof(resize_signature)))return Fail();
    // Native1417ED0A0 searches active membership, not UObject outer children.
    // Native1418272A0 is the sole native caller of the player tick. Existing
    // callback admission separately rejects pending/changed latent work.
    for(int i=0;i<type_pool_image_.count;++i) {
        const auto& e=type_effects_[i];if(!e.active_image.count)continue;
        ReplayHudPlayback now;
        if(e.active_image.count!=1 || !e.reconstructible || !TypePlayerBinding(e)
            || !TypeAssetReconstructible(e) || e.active->count!=1
            || e.active->data[0]!=e.players[0].object || e.stopped->count
            || e.active->capacity>16 || !e.active->data || e.private_backing.data
            || e.private_player.phase()!=ReplayHudPrivatePlayer::Phase::Empty
            || !ReplayHudPlayback::Read(TypePlayerImage(e),now) || !now.same_values(e.playback[0]))return Fail();
    }
    for(int i=0;i<type_pool_image_.count;++i) {
        const auto& e=type_effects_[i];
        if(e.active_image.count) {
            if(!e.private_player.Prepare(TypePlayerImage(e)))return Fail();
            e.private_backing=e.active_image;
        }
    }
    return Status::success();
}
Status ParkPrivateTypes() const {
    if(!ValidateTypeValues().ok())return Fail();
    for(int i=0;i<type_pool_image_.count;++i) {
        const auto& e=type_effects_[i];if(!e.active_image.count)continue;
        if(!e.private_player.Park(TypePlayerImage(e),[&]{
            *e.active={};
            // Verified native pointer-array allocator. New A/C backing may
            // shrink freely; B's exact allocation remains privately owned.
            reinterpret_cast<void(*)(void*,int)>(base_+0x1764ac0)(e.active,1);
            return e.active->data && e.active->capacity>=1 && e.active->capacity<=16 && !e.active->count;
        }))return Fail();
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] HUD private B type parked slot={} player={} backing_bytes={} unchanged=true\n"),i,e.players[0].index,e.private_backing.capacity*sizeof(Object*));
    }
    return ValidateTypes();
}
Status CommitPrivateTypes() const {
    for(int i=0;i<type_pool_image_.count;++i) {
        const auto& e=type_effects_[i];if(!e.active_image.count)continue;
        if(e.private_player.phase()==ReplayHudPrivatePlayer::Phase::Retired)continue;
        if(!TypePlayerBinding(e) || !TypeAssetReconstructible(e) || !TypePlayerAbsent(e.players[0].object))return Fail();
        if(!e.private_player.Commit(TypePlayerImage(e),[&]{
            if(!FinishReconstructedTypePlayer(e))return false;
            e.private_backing.count=0;
            reinterpret_cast<void(*)(void*,int)>(base_+0x1764ac0)(&e.private_backing,0);
            return !e.private_backing.data && !e.private_backing.capacity;
        }))return Fail();
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] HUD private B type retired slot={} player={} native_finish=true backing_released=true\n"),i,e.players[0].index);
    }
    return Status::success();
}
Status ValidateTypes() const {
    if(!type_pool_ || type_pool_->data!=type_pool_image_.data || type_pool_->count!=type_pool_image_.count
        || type_pool_->capacity!=type_pool_image_.capacity)return Fail();
    for(int i=0;i<type_pool_image_.count;++i) {
        const auto& e=type_effects_[i];
        if(type_pool_->data[i]!=e.widget.object || !Live(e.widget) || !Live(e.animation) || !Live(e.main))return Fail();
        auto** animation=e.widget.object->GetValuePtrByPropertyNameInChain<Object*>(L"DamgeEffect");
        auto** main=e.widget.object->GetValuePtrByPropertyNameInChain<Object*>(L"MAIN");
        bool attached{};
        if(!animation || *animation!=e.animation.object || !main || *main!=e.main.object || !TypeAttached(e,attached))return Fail();
        for(unsigned n=0;n<e.images.size();++n) {
            auto** image=e.widget.object->GetValuePtrByPropertyNameInChain<Object*>(type_images_[n]);
            if(!Live(e.images[n].object) || !image || *image!=e.images[n].object.object)return Fail();
        }
        if(!e.active || !e.stopped || !ValidArray(*e.active,4) || !ValidArray(*e.stopped,4) || e.stopped->count)return Fail();
        for(int j=0;j<e.active_image.count;++j) {
            auto* player=e.players[j].object;
            if(!Live(e.players[j]) || At<std::uintptr_t>(player,0)!=base_+0x373b998 || At<void*>(player,0x370)!=e.animation.object
                || (At<unsigned char>(player,0x761)&1) || At<int>(player,0x770))return Fail();
            if(e.retirement[j]==TypeEffect::Retirement::Finished && At<int>(player,0x6d8))return Fail();
        }
        const auto phase=e.private_player.phase();
        if(phase==ReplayHudPrivatePlayer::Phase::Prepared || phase==ReplayHudPrivatePlayer::Phase::Private
            || phase==ReplayHudPrivatePlayer::Phase::Recovered) {
            if(e.active_image.count!=1 || !e.private_player.Unchanged(TypePlayerImage(e)) || !TypePlayerBinding(e))return Fail();
            if(phase==ReplayHudPrivatePlayer::Phase::Private && (!TypePlayerAbsent(e.players[0].object)
                || e.private_backing.data!=e.active_image.data || e.private_backing.count!=1
                || e.private_backing.capacity!=e.active_image.capacity || !e.private_backing.data
                || e.private_backing.data[0]!=e.players[0].object || e.active->data==e.private_backing.data))return Fail();
        }
    }
    return Status::success();
}
Status ValidateTypeValues(const Prepared* prepared=nullptr) const {
    if(prepared && prepared->target!=this)return Fail();
    if(!ValidateTypes().ok())return Fail();
    for(int i=0;i<type_pool_image_.count;++i) {
        const auto& e=type_effects_[i];bool attached{};if(!TypeAttached(e,attached))return Fail();
        const auto transform=TypeTransform(e,attached);
        if(e.active->count!=e.active_image.count || e.stopped->count
            || At<unsigned char>(e.main.object,0x91)!=e.visibility
            || std::memcmp(&At<unsigned>(e.widget.object,0xb0),transform.data(),28))return Fail();
        for(int j=0;j<e.active_image.count;++j) {
            const auto id=prepared?prepared->type_players[i].identity:e.players[j];
            if(!id.object || !Live(id) || e.active->data[j]!=id.object)return Fail();
            if(prepared) {
                ReplayHudPlayback observed;
                if(!ReplayHudPlayback::Read({reinterpret_cast<const std::byte*>(id.object),0x780},observed)
                    || !observed.same_values(e.playback[j]))return Fail();
            }
        }
        for(const auto& row:e.images)if(std::memcmp(&At<unsigned>(row.object.object,0x1c0),row.color.data(),16)
            || std::memcmp(&At<unsigned>(row.object.object,0xb0),row.transform.data(),28))return Fail();
    }
    return Status::success();
}
Status FinishDiscardedTypes(const Sc6ReplayHudState* preserve=nullptr) const {
    if(!ValidateTypes().ok())return Fail();
    if(this==preserve)return Status::success(); // no C image: A journal owns partial publication
    for(int i=0;i<type_pool_image_.count;++i) {
        const auto& e=type_effects_[i];
        // Use current, captured C backing; never republish an old TArray header.
        if(e.active->data!=e.active_image.data || e.active->capacity!=e.active_image.capacity
            || e.stopped->data!=e.stopped_image.data || e.stopped->capacity!=e.stopped_image.capacity)return Fail();
        for(int j=0;j<e.active_image.count;++j) {
            bool private_b{};
            if(preserve)for(int n=0;n<preserve->type_pool_image_.count;++n) {
                const auto& b=preserve->type_effects_[n];
                if(b.active_image.count && b.players[0]==e.players[j]) {
                    if(!b.private_player.Unchanged(TypePlayerImage(b)))return Fail();
                    private_b=true;break;
                }
            }
            if(private_b)continue; // cancellation before/partway through HUD publication
            if(e.retirement[j]==TypeEffect::Retirement::Finished)continue;
            e.retirement[j]=TypeEffect::Retirement::Finishing;
            // 1418010D0: status=Stopped, Finish evaluation root, drain pending.
            // Unlike14181F0B0 it neither evaluates time0 nor broadcasts the
            // player/animation finished delegates. Pending/evaluating rejects.
            reinterpret_cast<void(*)(void*)>(base_+0x18010d0)(e.players[j].object);
            if(At<int>(e.players[j].object,0x6d8) || At<int>(e.players[j].object,0x770)
                || (At<unsigned char>(e.players[j].object,0x761)&1))return Fail();
            e.retirement[j]=TypeEffect::Retirement::Finished;
        }
    }
    return Status::success();
}
Status InstallTypeValues(bool validate=true,bool recover=false) const {
    if(!ValidateTypes().ok())return Fail();
    for(int i=0;i<type_pool_image_.count;++i) {
        const auto& e=type_effects_[i];
        if(recover && e.active_image.count) {
            if(e.active_image.count!=1
                || !e.private_player.Recover(TypePlayerImage(e),[&]{
                    if(!e.private_backing.data || e.private_backing.count!=1 || e.private_backing.data[0]!=e.players[0].object
                        || !ValidArray(*e.active,4) || e.active->capacity>16 || e.active->data==e.private_backing.data)return false;
                    e.active->count=0;
                    reinterpret_cast<void(*)(void*,int)>(base_+0x1764ac0)(e.active,0);
                    if(e.active->data || e.active->capacity)return false;
                    *e.active=e.private_backing;e.private_backing={};
                    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] HUD private B type recovered slot={} player={} unchanged=true original_backing=true\n"),i,e.players[0].index);
                    return true;
                }) || e.active->count!=1 || e.active->data[0]!=e.players[0].object)return Fail();
        } else e.active->count=0;
        bool attached{};if(!TypeAttached(e,attached))return Fail();const auto transform=TypeTransform(e,attached);
        reinterpret_cast<void(*)(void*,const void*)>(base_+0x181c380)(e.widget.object,transform.data());
        for(const auto& row:e.images) {
            reinterpret_cast<void(*)(void*,const void*)>(base_+0x18180b0)(row.object.object,row.color.data());
            reinterpret_cast<void(*)(void*,const void*)>(base_+0x181c380)(row.object.object,row.transform.data());
        }
        reinterpret_cast<void(*)(void*,unsigned char)>(base_+0x181e1b0)(e.main.object,e.visibility);
        reinterpret_cast<void(*)(void*)>(base_+0x17f3f30)(e.widget.object);
    }
    return validate?ValidateTypeValues():Status::success();
}
