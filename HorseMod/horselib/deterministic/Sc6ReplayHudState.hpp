#pragma once
#include "Sc6ReplayObjectLease.hpp"
#include "ReplayHudPlayback.hpp"
#include "ReplayHudOwnerRoute.hpp"
#include "NativeReplayWidgetClock.hpp"
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UFunction.hpp>
#include <array>
#include <vector>
#include <cstring>
#include <cwchar>
#include <utility>
#include <source_location>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>

namespace Horse::Deterministic {
// Bounded Cockpit damage-history participant. Native1418272A0 admits animation
// through ActiveSequencePlayers. Retained TypeEff players use guarded native
// evaluation reconstruction; B players remain untouched and retained for undo.
class Sc6ReplayHudState final {
    using Object=RC::Unreal::UObject;
    struct Array {Object** data{};int count{},capacity{};};
    struct ObjectId {
        Object* object{};int index{},serial{};
        friend bool operator==(const ObjectId&,const ObjectId&)=default;
    };
    enum class DamageBackingPhase : unsigned char {Attached,Private,Recovered,Retiring,Retired,Poisoned};
    struct Effect {
        ObjectId widget,image,slot,animation;
        Array* active{};Array* stopped{};
        Array active_image{},stopped_image{};
        std::array<ObjectId,4> players{};
        std::array<std::array<std::byte,0x780>,4> player_witness{}; // read-only owner witness, never installed
        std::array<ReplayHudPlayback,4> playback{};
        std::array<ObjectId,2> source_sections{},source_tracks{};
        ObjectId source_movie{},source_animation{};
        bool reconstructible{};
        mutable std::array<bool,4> evaluation_finished{};
        std::array<unsigned,4> color{},offsets{};
        std::array<unsigned,7> transform{};
        bool unused_image{};
        void* slate_image{};void* slate_slot{};
        mutable Array private_backing{};
        mutable DamageBackingPhase damage_backing{DamageBackingPhase::Attached};
    };
    inline static constexpr const wchar_t* fields_[]{L"PrevP1VitalPer",L"PrevP2VitalPer",L"PrevComboP1VitalPer",L"PrevComboP2VitalPer",L"StartingTimer",L"DmgEffCount",L"TypeEffCount"};
    std::uintptr_t base_{};void* world_{};DWORD thread_{};
    ObjectId cockpit_{};
    unsigned char cockpit_visibility_{};
    unsigned char cockpit_requested_visibility_{};
    ObjectId hud_manager_{},controller_{},cockpit_actor_{};
    void* battle_{};
    ObjectId damage_listener_{};
    struct ScriptBinding {int index{},serial{};std::uint64_t function{};};
    static_assert(sizeof(ScriptBinding)==16);
    std::array<Array,2> damage_delegates_{};
    std::array<std::array<ScriptBinding,64>,2> damage_bindings_{};
    std::array<std::array<ObjectId,64>,2> damage_receivers_{};
    std::array<unsigned char,5> damage_latches_{};
    std::array<unsigned*,7> addresses_{};
    std::array<unsigned,7> values_{};
    std::array<Object**,2> reference_addresses_{};
    std::array<ObjectId,2> references_{};
    Array* pool_{};Array pool_image_{};
    std::array<Effect,16> effects_{};
    struct TypeEffect {
        struct Image { ObjectId object;std::array<unsigned,4> color{};std::array<unsigned,7> transform{}; };
        enum class Retirement : unsigned char { Retained, Finishing, Finished };
        ObjectId widget,animation,main;
        std::array<Image,6> images{};
        std::array<unsigned,7> transform{};
        unsigned char visibility{};
        bool unused{};
        Array* active{};Array* stopped{};
        Array active_image{},stopped_image{};
        std::array<ObjectId,4> players{};
        mutable std::array<Retirement,4> retirement{};
        // Immutable scalar continuation survives native completion of the
        // retained original player. Active A admission remains guarded below.
        std::array<ReplayHudPlayback,4> playback{};
        // Compiled instance templates may refer to cooked archetype sections.
        // Retain that exact input graph; never install or rewrite its objects.
        std::array<ObjectId,13> source_sections{},source_tracks{};
        ObjectId source_movie{},source_animation{};
        bool reconstructible{};
        mutable ReplayHudPrivatePlayer private_player;
        mutable Array private_backing{}; // detached native allocation, never a stale restore address
    };
    Array* type_pool_{};Array type_pool_image_{};
    std::array<TypeEffect,16> type_effects_{};
    struct Announcement {
        enum class Life : unsigned char { Attached, Private, Finishing, Removing, Removed };
        ObjectId widget,klass;
        Array active{},stopped{};
        std::array<ObjectId,4> players{},animations{};
        std::array<std::array<std::byte,0x780>,4> witnesses{};
        std::array<std::byte,16> clock{};
        void* viewport{};void* viewport_control{};
        unsigned char visibility{};
        mutable Life life{Life::Attached};
        mutable unsigned finish_cursor{};
    };
    ObjectId announce_actor_{};
    unsigned char announce_enabled_{};
    std::array<ObjectId,15> announce_classes_{};
    std::array<Announcement,16> announcements_{};
    unsigned announcement_count_{};
public:
    struct Prepared {
        const Sc6ReplayHudState* target{};
        const Sc6ReplayHudState* private_hud{};
        std::array<ReplayHudPlayerOperation,16> players{};
        enum class TypeBacking : unsigned char { Empty, Allocating, Owned, Recovering, Recovered, Adopted };
        std::array<TypeBacking,16> type_backings{};
        struct DamagePlayer {
            enum class Phase : unsigned char { Empty, Constructing, Constructed, Starting, Published, Finishing, Finished, Released };
            Phase phase{};
            ObjectId identity{};
            std::shared_ptr<Sc6ReplayObjectLease> lease;
        };
        std::array<DamagePlayer,16> damage_players{};
        std::array<DamagePlayer,16> type_players{};
        bool Commit() {
            for(auto& player:players)if(!player.Commit())return false;
            return !private_hud || (target && target->CommitTypeBackings(*this) && private_hud->CommitPrivateTypes().ok() && private_hud->CommitDamageBackings().ok() && private_hud->CommitAnnouncements().ok()
                && target && target->ReleaseDamagePlayers(*this) && target->ReleaseTypePlayers(*this));
        }
    };
private:
    Sc6ReplayObjectLease lease_;
    template<class T> static T& At(void* p,std::size_t n) {return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+n);}
    ObjectId Id(Object* p) const {
        if(!p) return {};
        auto* item=RC::Unreal::FUObjectArray::IndexToObject(p->GetInternalIndex());
        if(!item || item->GetUObject()!=p || !item->IsValid(false)) return {};
        std::array<int,2> weak{};
        // A never-before-weak UObject can have serial zero. The lease uses
        // this native constructor too; bind before recording the identity.
        reinterpret_cast<void(*)(void*,const void*)>(base_+0xf7bad0)(weak.data(),p);
        if(weak[0]!=p->GetInternalIndex() || weak[1]!=item->GetSerialNumber()) return {};
        return {p,weak[0],weak[1]};
    }
    static bool Live(const ObjectId& id) {
        if(!id.object) return true;
        auto* item=RC::Unreal::FUObjectArray::IndexToObject(id.index);
        return item && item->GetUObject()==id.object && item->IsValid(false) && item->GetSerialNumber()==id.serial;
    }
    static bool ValidArray(const Array& a,int cap) {return a.count>=0 && a.count<=cap && a.capacity>=a.count && (!a.count || a.data);}
    static bool ValidObject(Object* p) {
        const auto* item=p?RC::Unreal::FUObjectArray::IndexToObject(p->GetInternalIndex()):nullptr;
        return item && item->GetUObject()==p && item->IsValid(false);
    }
    static ReplayHudOwnerRoute ReadHudOwners(std::uintptr_t base,void* battle) {
        const auto read=[](std::uintptr_t address,void* output,std::size_t size) noexcept {
            __try {std::memcpy(output,reinterpret_cast<const void*>(address),size);return true;}
            __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
        };
        auto route=InspectReplayHudOwnerRoute(read,base,reinterpret_cast<std::uintptr_t>(battle));
        if(!route.valid)return route;
        auto* manager=reinterpret_cast<Object*>(route.manager);
        route.valid=ValidObject(manager) && ValidObject(reinterpret_cast<Object*>(route.controller))
            && ValidObject(reinterpret_cast<Object*>(route.damage_listener))
            && static_cast<Object*>(battle)->GetValuePtrByPropertyNameInChain<Object*>(L"BattleHUDManager")==&At<Object*>(battle,0x530)
            && manager->GetValuePtrByPropertyNameInChain<Object*>(L"HUDController")==&At<Object*>(manager,0x3b8)
            && manager->GetValuePtrByPropertyNameInChain<Object*>(L"DamageInfoEventListener")==&At<Object*>(manager,0x3a0);
        return route;
    }
    static bool ReadCockpitRoute(std::uintptr_t base,void* world,void* battle,
        Object* controller,Object*& actor,Object*& widget) {
        actor=widget=nullptr;
        if(!base || !world || !battle || !ValidObject(controller)
            || At<void*>(controller,0x98)!=battle || At<std::uintptr_t>(controller,0)!=base+0x327e810)return false;
        const auto* slots=controller->GetValuePtrByPropertyNameInChain<Array>(L"BattleHUD");
        if(slots!=&At<Array>(controller,0x390) || !ValidArray(*slots,8) || slots->count!=8)return false;
        actor=slots->data[0];
        if(!ValidObject(actor) || At<std::uintptr_t>(actor,0)!=base+0x327e1e8
            || At<void*>(actor,0x98)!=battle)return false;
        // Native1403EC0D0/1403FBC10 create and consume this instance slot.
        // A separately retained historical widget is not the active instance.
        widget=At<Object*>(actor,0x398);
        if(!ValidObject(widget) || At<void*>(widget,0x20)!=At<void*>(world,0x140))return false;
        const RC::Unreal::FName name(L"CockpitBase_C");
        for(auto* type=static_cast<RC::Unreal::UStruct*>(widget->GetClassPrivate());type;type=type->GetSuperStruct())
            if(type->GetNamePrivate().Equals(name))return true;
        return false;
    }
    static bool ResolveCockpit(std::uintptr_t base,void* world,void* battle,
        Object*& controller,Object*& actor,Object*& widget) {
        controller=actor=widget=nullptr;
        if(!base || !world || !battle)return false;
        const auto owners=ReadHudOwners(base,battle);
        if(!owners.valid)return false;
        controller=reinterpret_cast<Object*>(owners.controller);
        return ReadCockpitRoute(base,world,battle,controller,actor,widget);
    }
    bool Attached(const Effect& e,bool& attached) const {
        const auto live=[](Object* p) {
            auto* item=p?RC::Unreal::FUObjectArray::IndexToObject(p->GetInternalIndex()):nullptr;
            return item && item->GetUObject()==p && item->IsValid(false);
        };
        auto** slot=e.widget.object->GetValuePtrByPropertyNameInChain<Object*>(L"Slot");
        if(!slot) return false;
        attached=*slot!=nullptr;
        if(!attached) return true;
        if(!live(*slot) || (*slot)->GetClassPrivate()->GetName()!=L"CanvasPanelSlot") return false;
        auto** parent=(*slot)->GetValuePtrByPropertyNameInChain<Object*>(L"Parent");
        auto** content=(*slot)->GetValuePtrByPropertyNameInChain<Object*>(L"Content");
        auto** p1=cockpit_.object->GetValuePtrByPropertyNameInChain<Object*>(L"p1_vital_gauge_root");
        auto** p2=cockpit_.object->GetValuePtrByPropertyNameInChain<Object*>(L"p2_vital_gauge_root");
        return parent && live(*parent) && content && *content==e.widget.object && p1 && p2
            && (*parent==*p1 || *parent==*p2);
    }
    std::array<unsigned,7> PresentationTransform(const Effect& e,bool attached) const {
        auto value=e.transform;
        // An unused unparented image has no visible area. Keep native widget,
        // slot and sequence-player lifetimes intact when later backing exists.
        // Cooked CreateDamageEffect writes all seven transform fields before
        // reusing the image; neither this transform nor its alpha feeds combat.
        if(e.unused_image && attached) value[2]=value[3]=0;
        return value;
    }
    static Status Fail(std::source_location at=std::source_location::current()) {
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] HUD transaction rejected line={}\n"),at.line());
        return Status::failure(FailureCode::RestorePreflightFailed);
    }
    Status CaptureDamageListener(std::vector<void*>& owners) {
        const auto route=ReadHudOwners(base_,battle_);
        if(!route.valid || route.controller!=reinterpret_cast<std::uintptr_t>(controller_.object))return Fail();
        ReplayHudOwnerCensus census(route);
        const RC::Unreal::FName controller_class(L"LuxBattleHUDController"),listener_class(L"LuxDamageInfoEventListener");
        bool complete=true;
        RC::Unreal::UObjectGlobals::ForEachUObject([&](Object* object,std::int32_t,std::int32_t) {
            using namespace RC::Unreal;
            if(!object || object->HasAnyFlags(static_cast<EObjectFlags>(RF_ClassDefaultObject|RF_ArchetypeObject)))return RC::LoopAction::Continue;
            auto* type=object->GetClassPrivate();if(!type)return RC::LoopAction::Continue;
            const auto match=census.MatchClass(reinterpret_cast<std::uintptr_t>(type),[&](std::uintptr_t address) {
                unsigned result{};
                for(auto* parent=reinterpret_cast<UStruct*>(address);parent;parent=parent->GetSuperStruct()) {
                    const auto name=parent->GetNamePrivate();
                    if(name.Equals(controller_class))result|=1;
                    if(name.Equals(listener_class))result|=2;
                }
                return result;
            });
            if(!match || object->IsA<UClass>() || !ValidObject(object) || At<void*>(object,0x98)!=battle_)return RC::LoopAction::Continue;
            if(!census.Observe(reinterpret_cast<std::uintptr_t>(object),match)) {complete=false;return RC::LoopAction::Break;}
            return RC::LoopAction::Continue;
        });
        if(!complete || !census.complete())return Fail();
        hud_manager_=Id(reinterpret_cast<Object*>(route.manager));if(!hud_manager_.object)return Fail();
        owners.push_back(hud_manager_.object);
        auto* listener=reinterpret_cast<Object*>(route.damage_listener);
        damage_listener_=Id(listener);if(!damage_listener_.object)return Fail();
        owners.push_back(listener);
        for(unsigned i=0;i<2;++i) {
            auto* array=listener->GetValuePtrByPropertyNameInChain<Array>(i?L"OnDamageTypeEvent":L"OnDamageEvent");
            if(array!=&At<Array>(listener,0x388+i*16) || !ValidArray(*array,64))return Fail();
            damage_delegates_[i]=*array;
            if(array->count)std::memcpy(damage_bindings_[i].data(),array->data,std::size_t(array->count)*16);
            for(int j=0;j<array->count;++j) {
                const auto& row=damage_bindings_[i][j];
                if(row.index<0 || !row.serial || !row.function)return Fail();
                const auto* item=RC::Unreal::FUObjectArray::IndexToObject(row.index);
                if(!item || !item->IsValid(false) || item->GetSerialNumber()!=row.serial || !item->GetUObject())return Fail();
                damage_receivers_[i][j]={item->GetUObject(),row.index,row.serial};
                owners.push_back(item->GetUObject());
            }
        }
        // Native callbacks1403F2FE0/1403F2F10 set four per-player event
        // latches and the immediate-dispatch flag.1403FD4A0 consumes the
        // pending events;1403FC280 clears the immediate flag after its tick.
        // Preserve pending delivery, never synthesize events from presentation.
        std::memcpy(damage_latches_.data(),&At<unsigned char>(listener,0x3a8),damage_latches_.size());
        for(auto value:damage_latches_)if(value>1)return Fail();
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] HUD event latches captured values={}/{}/{}/{}/{} delegates={}/{}\n"),
            damage_latches_[0],damage_latches_[1],damage_latches_[2],damage_latches_[3],damage_latches_[4],
            damage_delegates_[0].count,damage_delegates_[1].count);
        return Status::success();
    }
    Status ValidateDamageListener() const {
        if(!damage_listener_.object || !Live(damage_listener_)
            || At<std::uintptr_t>(damage_listener_.object,0)!=base_+0x327b740
            || At<void*>(damage_listener_.object,0x98)!=battle_)return Fail();
        for(unsigned i=0;i<2;++i) {
            const auto& current=At<Array>(damage_listener_.object,0x388+i*16);
            const auto& saved=damage_delegates_[i];
            if(current.data!=saved.data || current.count!=saved.count || current.capacity!=saved.capacity
                || (current.count && std::memcmp(current.data,damage_bindings_[i].data(),std::size_t(current.count)*16)))return Fail();
            for(int j=0;j<saved.count;++j)if(!Live(damage_receivers_[i][j]))return Fail();
        }
        return Status::success();
    }
#include "Sc6ReplayHudState.ActiveTypes.inl"
#include "Sc6ReplayHudState.TypeEffects.inl"
#include "Sc6ReplayHudState.ActiveDamage.inl"
#include "Sc6ReplayHudState.Announcements.inl"
public:
    // Placement hint only. Full capture and restore admission remain mandatory.
    // Read native active/stopped-player membership without retaining or changing
    // a player, evaluating an animation, or allocating a historical image.
    static Status InspectTargetEligibility(std::uintptr_t base,void* world,void* battle) {
        if(!world)return Status::failure(FailureCode::ContextUnavailable);
        const auto valid=[](Object* p) {
            auto* item=p?RC::Unreal::FUObjectArray::IndexToObject(p->GetInternalIndex()):nullptr;
            return item && item->GetUObject()==p && item->IsValid(false);
        };
        Object *controller{},*actor{},*cockpit{};
        if(!ResolveCockpit(base,world,battle,controller,actor,cockpit))return Status::failure(FailureCode::GenerationMismatch);
        bool busy{};
        for(const auto name:{L"DmgEff",L"DmgTypeEff"}) {
            const auto* pool=cockpit->GetValuePtrByPropertyNameInChain<Array>(name);
            if(!pool || !ValidArray(*pool,16) || !pool->count)return Status::failure(FailureCode::GenerationMismatch);
            for(int i=0;i<pool->count;++i) {
                auto* widget=pool->data[i];if(!valid(widget))return Status::failure(FailureCode::GenerationMismatch);
                const auto* active=widget->GetValuePtrByPropertyNameInChain<Array>(L"ActiveSequencePlayers");
                const auto* stopped=widget->GetValuePtrByPropertyNameInChain<Array>(L"StoppedSequencePlayers");
                if(!active || !stopped || !ValidArray(*active,4) || !ValidArray(*stopped,4))return Status::failure(FailureCode::GenerationMismatch);
                ReplayHudPlayback playback;
                const bool type_candidate=std::wcscmp(name,L"DmgTypeEff")==0 && active->count==1 && !stopped->count
                    && valid(active->data[0]) && At<std::uintptr_t>(active->data[0],0)==base+0x373b998
                    && ReplayHudPlayback::Read({reinterpret_cast<const std::byte*>(active->data[0]),0x780},playback)
                    && playback.forward_first_loop();
                // Placement hint only. Capture/retention still require the
                // compiled asset and native ownership checks, then preparation
                // must prove this player is detached from B before any write.
                busy|=(active->count!=0 || stopped->count!=0) && !type_candidate;
                if(active->count || stopped->count) {
                    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] HUD checkpoint eligibility pool={} slot={} active={} stopped={}\n"),
                        name,i,active->count,stopped->count);
                    for(int j=0;j<active->count;++j) {
                        auto* player=active->data[j];
                        // Failure-only observation of an independently typed
                        // live player. No evaluation, retention or admission
                        // permission follows from these diagnostic values.
                        if(!valid(player) || At<std::uintptr_t>(player,0)!=base+0x373b998)continue;
                        auto* animation=At<Object*>(player,0x370);
                        if(!valid(animation) || animation->GetClassPrivate()->GetName()!=L"WidgetAnimation")continue;
                        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] HUD checkpoint active pool={} slot={} player={} animation={} time={:.9f} end={:.9f} status={} loops={}/{} rate={:.9f} mode={} forward={} pending={} evaluating={} start_delegates={} finish_delegates={}\n"),
                            name,i,j,animation->GetName(),At<double>(player,0x6a0),At<double>(player,0x6a8),At<int>(player,0x6d8),
                            At<int>(player,0x754),At<int>(player,0x750),At<float>(player,0x758),At<int>(player,0x75c),
                            At<unsigned char>(player,0x760),At<int>(player,0x770),At<unsigned char>(player,0x761)&1,
                            At<int>(animation,0x338),At<int>(animation,0x348));
                        ReplayHudPlayback logical;
                        if(ReplayHudPlayback::Read({reinterpret_cast<const std::byte*>(player),0x780},logical))
                            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] HUD checkpoint logical pool={} slot={} player={} offset={:.17g} lower={:.17g} upper={:.17g} lower_kind={} upper_kind={} scalar_admitted={}\n"),
                                name,i,j,logical.offset,logical.lower,logical.upper,logical.lower_kind,logical.upper_kind,logical.forward_first_loop());
                    }
                }
            }
        }
        return busy?Status::failure(FailureCode::UnsupportedContent):Status::success();
    }
    std::size_t owned_bytes() const {
        auto bytes=sizeof(*this)+lease_.owned_bytes();
        for(const auto& e:type_effects_)if(e.active_image.count)bytes+=16*sizeof(Object*)+(1u<<20); // independent native player, lease and bounded evaluation/backing reservation
        // Reserve before publication, including native capacity rounding. B's
        // backing becomes private without allocating a duplicate player image.
        for(const auto& e:effects_)if(e.active_image.count && e.damage_backing!=DamageBackingPhase::Retired)
            bytes+=std::size_t(e.active_image.capacity)*sizeof(Object*)+(1u<<20);
        for(const auto& e:type_effects_)if(e.private_player.phase()==ReplayHudPrivatePlayer::Phase::Private
            || e.private_player.phase()==ReplayHudPrivatePlayer::Phase::Retiring)
            bytes+=std::size_t(e.private_backing.capacity)*sizeof(Object*);
        return bytes;
    }
    Status ReleaseCapture() {
        if(thread_ && thread_!=GetCurrentThreadId())return Fail();
        if(replay_hud_widget_admission.OwnedBy(this))return Fail();
        for(const auto& e:effects_)if(e.damage_backing==DamageBackingPhase::Private
            || e.damage_backing==DamageBackingPhase::Retiring || e.damage_backing==DamageBackingPhase::Poisoned)return Fail();
        for(const auto& e:type_effects_)if(e.private_player.phase()==ReplayHudPrivatePlayer::Phase::Private
            || e.private_player.phase()==ReplayHudPrivatePlayer::Phase::Retiring)return Fail();
        auto status=lease_.Release();if(!status.ok())return status;
        cockpit_={};cockpit_visibility_=cockpit_requested_visibility_=0;hud_manager_={};controller_={};cockpit_actor_={};battle_=nullptr;addresses_={};values_={};reference_addresses_={};references_={};
        pool_=nullptr;pool_image_={};effects_={};base_=0;world_=nullptr;thread_=0;
        type_pool_=nullptr;type_pool_image_={};type_effects_={};
        damage_listener_={};damage_delegates_={};damage_bindings_={};damage_receivers_={};damage_latches_={};
        announce_actor_={};announce_enabled_=0;announce_classes_={};announcements_={};announcement_count_=0;
        return Status::success();
    }
    const char* historical_target_limitation() const noexcept {
        // Capture also supplies complete B undo, which may own active players.
        // Such an image is valid captured state, but not an implemented A
        // restoration domain. No live storage is read for this shape check.
        if(!cockpit_.object || pool_image_.count<1 || pool_image_.count>16) return "cockpit_or_damage_pool";
        if(announcement_count_)return "active_announcement_reconstruction";
        for(int i=0;i<pool_image_.count;++i) if(effects_[i].active_image.count && !effects_[i].reconstructible) return "active_damage_sequence";
        if(type_pool_image_.count<1 || type_pool_image_.count>16)return "type_pool_shape";
        for(int i=0;i<type_pool_image_.count;++i)
            if(type_effects_[i].active_image.count && !type_effects_[i].reconstructible)return "active_type_sequence";
        return nullptr;
    }
    bool supports_historical_target() const noexcept {return historical_target_limitation()==nullptr;}
    Status Capture(std::uintptr_t base,void* world,void* battle,std::size_t budget) {
        if(lease_.registered() || budget<sizeof(*this)) return Fail();
        base_=base;world_=world;battle_=battle;thread_=GetCurrentThreadId();
        constexpr unsigned char transform_signature[]{0x0f,0x10,0x02,0x0f,0x11,0x81,0xb0,0,0,0,0xf2,0x0f,0x10,0x4a,0x10,0xf2};
        if(std::memcmp(reinterpret_cast<void*>(base+0x181c380),transform_signature,sizeof(transform_signature))) return Fail();
        Object *controller{},*actor{},*widget{};
        if(!ResolveCockpit(base,world,battle,controller,actor,widget))return Fail();
        controller_=Id(controller);cockpit_actor_=Id(actor);cockpit_=Id(widget);
        if(!controller_.object || !cockpit_actor_.object || !cockpit_.object)return Fail();
        auto* p=cockpit_.object;
        auto* root_visibility=p->GetValuePtrByPropertyNameInChain<unsigned char>(L"Visibility");
        if(root_visibility!=&At<unsigned char>(p,0x91) || *root_visibility>4)return Fail();
        cockpit_visibility_=*root_visibility;
        constexpr unsigned char requested_signature[]{0x88,0x91,0x88,0x03,0x00,0x00,0xc3};
        if(std::memcmp(reinterpret_cast<void*>(base+0x3fa280),requested_signature,sizeof(requested_signature))
            || At<std::uintptr_t>(reinterpret_cast<void*>(base+0x327e1e8),0x608)!=base+0x3fa280)return Fail();
        cockpit_requested_visibility_=At<unsigned char>(actor,0x388);
        if(cockpit_requested_visibility_>1)return Fail();
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] HUD visibility source tick={} requested={} root={} read_only=true\n"),
            *reinterpret_cast<const unsigned*>(base+0x470d0c4),cockpit_requested_visibility_,cockpit_visibility_);
        for(unsigned i=0;i<7;++i) {
            addresses_[i]=p->GetValuePtrByPropertyNameInChain<unsigned>(fields_[i]);
            if(!addresses_[i]) return Fail();values_[i]=*addresses_[i];
        }
        constexpr std::size_t maximum_owners=4+2+16*(4+4+6)+16*(9+4+28)+1+2*64+16+16*10;
        constexpr auto owner_scratch=maximum_owners*sizeof(void*)+sizeof(ReplayHudOwnerCensus);
        if(owner_scratch>budget-sizeof(*this)) return Fail();
        std::vector<void*> owners;owners.reserve(maximum_owners);owners.push_back(p);owners.push_back(controller);owners.push_back(actor);
        for(unsigned i=0;i<2;++i) {
            reference_addresses_[i]=p->GetValuePtrByPropertyNameInChain<Object*>(i?L"CurrentP2DamageEffect":L"CurrentP1DamageEffect");
            if(!reference_addresses_[i]) return Fail();
            auto* value=*reference_addresses_[i];references_[i]=Id(value);
            if(value && !references_[i].object) return Fail();
            if(value) owners.push_back(value);
        }
        pool_=p->GetValuePtrByPropertyNameInChain<Array>(L"DmgEff");
        if(!pool_ || !ValidArray(*pool_,16) || !pool_->count) return Fail();pool_image_=*pool_;
        for(int i=0;i<pool_->count;++i) {
            auto& e=effects_[i];e.widget=Id(pool_->data[i]);if(!e.widget.object) return Fail();
            auto** image=e.widget.object->GetValuePtrByPropertyNameInChain<Object*>(L"damage");
            auto** animation=e.widget.object->GetValuePtrByPropertyNameInChain<Object*>(L"fall_out");
            if(!image || !animation) return Fail();e.image=Id(*image);e.animation=Id(*animation);
            if(!e.image.object || !e.animation.object || e.image.object->GetClassPrivate()->GetName()!=L"Image") return Fail();
            auto** slot=e.image.object->GetValuePtrByPropertyNameInChain<Object*>(L"Slot");
            if(!slot) return Fail();e.slot=Id(*slot);if(!e.slot.object || e.slot.object->GetClassPrivate()->GetName()!=L"CanvasPanelSlot") return Fail();
            // Native reflected UImage.ColorAndOpacity+1C0 and CanvasPanelSlot
            // LayoutData.Offsets+38. Bindings must agree before using setters.
            if(e.image.object->GetValuePtrByPropertyNameInChain<unsigned>(L"ColorAndOpacity")!=&At<unsigned>(e.image.object,0x1c0)
                || e.slot.object->GetValuePtrByPropertyNameInChain<unsigned>(L"LayoutData")!=&At<unsigned>(e.slot.object,0x38)) return Fail();
            std::memcpy(e.color.data(),&At<unsigned>(e.image.object,0x1c0),16);
            std::memcpy(e.offsets.data(),&At<unsigned>(e.slot.object,0x38),16);
            if(e.image.object->GetValuePtrByPropertyNameInChain<unsigned>(L"RenderTransform")!=&At<unsigned>(e.image.object,0xb0)) return Fail();
            std::memcpy(e.transform.data(),&At<unsigned>(e.image.object,0xb0),28);
            bool attached{};if(!Attached(e,attached)) return Fail();e.unused_image=!attached;
            e.slate_image=At<void*>(e.image.object,0x1f0);e.slate_slot=At<void*>(e.slot.object,0x68);
            e.active=e.widget.object->GetValuePtrByPropertyNameInChain<Array>(L"ActiveSequencePlayers");
            e.stopped=e.widget.object->GetValuePtrByPropertyNameInChain<Array>(L"StoppedSequencePlayers");
            if(!e.active || !e.stopped || !ValidArray(*e.active,4) || e.active->capacity>16 || e.stopped->count) return Fail();
            e.active_image=*e.active;e.stopped_image=*e.stopped;
            if(e.unused_image && e.active_image.count) return Fail();
            for(int j=0;j<e.active->count;++j) {
                auto* player=e.active->data[j];e.players[j]=Id(player);
                if(!e.players[j].object || At<std::uintptr_t>(player,0)!=base+0x373b998
                    || At<void*>(player,0x370)!=e.animation.object || (At<unsigned char>(player,0x761)&1)
                    || At<int>(player,0x770)) return Fail();
                std::memcpy(e.player_witness[j].data(),player,0x780);owners.push_back(player);
                if(!ReplayHudPlayback::Read(e.player_witness[j],e.playback[j]))return Fail();
            }
            const bool scalar=e.active_image.count==1 && e.playback[0].forward_first_loop();
            const bool binding=scalar && DamagePlayerBinding(e,e.players[0]);
            const bool sources=binding && CaptureDamageSources(e,owners);
            e.reconstructible=sources && DamageAssetReconstructible(e);
            if(e.active_image.count)RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] HUD damage reconstruction capture slot={} active={} scalar={} binding={} sources={} admitted={} time={:.17g}\n"),
                i,e.active_image.count,scalar,binding,sources,e.reconstructible,e.playback[0].time);
            for(auto* o:{e.widget.object,e.image.object,e.slot.object,e.animation.object}) owners.push_back(o);
        }
        if(!CaptureTypes(owners).ok())return Fail();
        if(!CaptureDamageListener(owners).ok())return Fail();
        if(!CaptureAnnouncements(owners).ok())return Fail();
        return lease_.Acquire(base,owners,budget-sizeof(*this)-owner_scratch);
    }
    Status ValidateBindings(bool physical=true,bool historical=false) const {
        if(GetCurrentThreadId()!=thread_ || !Live(cockpit_) || !Live(hud_manager_) || !Live(controller_) || !Live(cockpit_actor_) || !lease_.Validate().ok()
            || !pool_ || pool_->data!=pool_image_.data || pool_->count!=pool_image_.count || pool_->capacity!=pool_image_.capacity) return Fail();
        if(!ValidateDamageListener().ok())return Fail();
        if(!ValidateAnnouncements().ok())return Fail();
        const auto route=ReadHudOwners(base_,battle_);
        if(!route.valid || route.manager!=reinterpret_cast<std::uintptr_t>(hud_manager_.object)
            || route.controller!=reinterpret_cast<std::uintptr_t>(controller_.object)
            || route.damage_listener!=reinterpret_cast<std::uintptr_t>(damage_listener_.object))return Fail();
        if(cockpit_.object->GetValuePtrByPropertyNameInChain<unsigned char>(L"Visibility")
            !=&At<unsigned char>(cockpit_.object,0x91) || cockpit_visibility_>4 || cockpit_requested_visibility_>1
            || At<std::uintptr_t>(cockpit_actor_.object,0)!=base_+0x327e1e8
            || At<std::uintptr_t>(reinterpret_cast<void*>(base_+0x327e1e8),0x608)!=base_+0x3fa280)return Fail();
        if(physical) {
            Object *actor{},*widget{};
            if(!ReadCockpitRoute(base_,world_,battle_,controller_.object,actor,widget)
                || actor!=cockpit_actor_.object || widget!=cockpit_.object)return Fail();
        }
        for(int i=0;i<pool_image_.count;++i) {
            const auto& e=effects_[i];
            if(!DamageBackingIntact(e))return Fail();
            if(pool_->data[i]!=e.widget.object || !Live(e.widget) || !Live(e.image) || !Live(e.slot) || !Live(e.animation)) {
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] HUD owner rejection slot={} membership={} widget={} image={} slot_live={} animation={}\n"),i,pool_->data[i]==e.widget.object,Live(e.widget),Live(e.image),Live(e.slot),Live(e.animation));
                return Fail();
            }
            if(physical && (At<void*>(e.image.object,0x1f0)!=e.slate_image || At<void*>(e.slot.object,0x68)!=e.slate_slot)) return Fail();
            auto** image=e.widget.object->GetValuePtrByPropertyNameInChain<Object*>(L"damage");
            auto** animation=e.widget.object->GetValuePtrByPropertyNameInChain<Object*>(L"fall_out");
            auto** slot=e.image.object->GetValuePtrByPropertyNameInChain<Object*>(L"Slot");
            if(!image || *image!=e.image.object || !animation || *animation!=e.animation.object || !slot || *slot!=e.slot.object) return Fail();
            bool attached{};if(!Attached(e,attached)) return Fail();
            for(int j=0;j<e.active_image.count;++j) {
                if(!Live(e.players[j])) return Fail();
                if(e.evaluation_finished[j]) {
                    if((e.damage_backing!=DamageBackingPhase::Retiring && e.damage_backing!=DamageBackingPhase::Retired)
                        || At<int>(e.players[j].object,0x6d8) || At<int>(e.players[j].object,0x400) || At<int>(e.players[j].object,0x410))return Fail();
                    continue;
                }
                if(historical && e.reconstructible) {
                    if(!DamageAssetReconstructible(e))return Fail();
                    continue;
                }
                if(std::memcmp(e.players[j].object,e.player_witness[j].data(),0x780)) {
                    // A failed preflight remains read-only. Record every changed
                    // word in this bounded retained owner before tracing its
                    // consumers; no raw player image is ever installed.
                    for(std::size_t offset=0;offset<0x780;offset+=8) {
                        std::uint64_t saved{},current{};
                        std::memcpy(&saved,e.player_witness[j].data()+offset,8);
                        std::memcpy(&current,static_cast<const std::byte*>(static_cast<const void*>(e.players[j].object))+offset,8);
                        if(saved!=current) RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] HUD active player difference effect={} player={} offset={:x} saved={:016x} current={:016x}\n"),i,j,offset,saved,current);
                    }
                    return Fail();
                }
            }
        }
        for(auto& r:references_) if(!Live(r)) return Fail();
        return ValidateTypes();
    }
    Status ValidateValues(const Prepared* prepared=nullptr) const {
        if(prepared && prepared->target!=this)return Fail();
        if(!ValidateBindings(false,prepared!=nullptr).ok()) return Fail();
        if(At<unsigned char>(cockpit_.object,0x91)!=cockpit_visibility_)return Fail();
        if(At<unsigned char>(cockpit_actor_.object,0x388)!=cockpit_requested_visibility_)return Fail();
        if(At<unsigned char>(announce_actor_.object,0x388)!=announce_enabled_)return Fail();
        if(std::memcmp(&At<unsigned char>(damage_listener_.object,0x3a8),damage_latches_.data(),damage_latches_.size()))return Fail();
        for(unsigned i=0;i<7;++i) if(*addresses_[i]!=values_[i]) return Fail();
        for(unsigned i=0;i<2;++i) if(*reference_addresses_[i]!=references_[i].object) return Fail();
        for(int i=0;i<pool_image_.count;++i) {
            const auto& e=effects_[i];
            bool attached{};if(!Attached(e,attached)) return Fail();
            const auto transform=PresentationTransform(e,attached);
            if(e.active->count!=e.active_image.count || e.stopped->count
                || std::memcmp(&At<unsigned>(e.image.object,0x1c0),e.color.data(),16)
                || std::memcmp(&At<unsigned>(e.image.object,0xb0),transform.data(),28)
                || std::memcmp(&At<unsigned>(e.slot.object,0x38),e.offsets.data(),16)) return Fail();
            for(int j=0;j<e.active->count;++j) {
                auto id=prepared?prepared->damage_players[i].identity:e.players[j];
                if(e.active->data[j]!=id.object || !Live(id))return Fail();
                if(prepared) {ReplayHudPlayback now;
                    if(!ReplayHudPlayback::Read({reinterpret_cast<const std::byte*>(id.object),0x780},now) || !now.same_values(e.playback[j]))return Fail();}
            }
        }
        return ValidateTypeValues(prepared);
    }
    Status Prepare(const Sc6ReplayHudState& b,Prepared& prepared) const {
        const auto unsupported=[&](const char* check,int slot=-1,int active=0) {
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] HUD preparation rejected check={} slot={} active={} before_publication=true\n"),RC::to_generic_string(check),slot,active);
            return Status::failure(FailureCode::UnsupportedContent);
        };
        if(!supports_historical_target()) return unsupported("historical_target");
        if(!SameTypes(b))return unsupported("type_membership");
        if(!ValidateBindings(false,true).ok() || !b.ValidateBindings().ok() || !b.ValidateValues().ok()
            || battle_!=b.battle_ || hud_manager_!=b.hud_manager_ || controller_!=b.controller_ || cockpit_actor_!=b.cockpit_actor_ || damage_listener_!=b.damage_listener_
            || cockpit_!=b.cockpit_ || pool_image_.count!=b.pool_image_.count) return Fail();
        for(int i=0;i<pool_image_.count;++i) {
            const auto& a=effects_[i];const auto& e=b.effects_[i];
            if(a.slate_image!=e.slate_image || a.slate_slot!=e.slate_slot)
                RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] HUD backing reconstruction slot={} image_A={:x} image_B={:x} slot_A={:x} slot_B={:x}\n"),i,reinterpret_cast<std::uintptr_t>(a.slate_image),reinterpret_cast<std::uintptr_t>(e.slate_image),reinterpret_cast<std::uintptr_t>(a.slate_slot),reinterpret_cast<std::uintptr_t>(e.slate_slot));
            if((a.active_image.count && !a.reconstructible) || (e.active_image.count && !e.reconstructible))return unsupported("damage_animation_inputs",i,a.active_image.count);
            if(a.widget!=e.widget || a.image!=e.image || a.slot!=e.slot || a.animation!=e.animation)return unsupported("damage_membership",i);
        }
        for(int i=0;i<pool_image_.count;++i)if(effects_[i].active_image.count) {
            if(prepared.damage_players[i].lease || prepared.damage_players[i].phase!=Prepared::DamagePlayer::Phase::Empty)return Fail();
            prepared.damage_players[i].lease=std::make_shared<Sc6ReplayObjectLease>();
        }
        auto status=PrepareTypePlayers(b,prepared);if(!status.ok())return status;
        status=b.PreparePrivateTypes();if(!status.ok())return status;
        if(announce_actor_!=b.announce_actor_ || announce_classes_!=b.announce_classes_ || !replay_hud_widget_admission.empty())return Fail();
        prepared.private_hud=&b;return Status::success();
    }
    static void ResizeDamageBacking(Array& array,std::uintptr_t base) {
        reinterpret_cast<void(*)(void*,int)>(base+0x1764ac0)(&array,0);
    }
    void ResizeDamageBacking(Array& array) const {ResizeDamageBacking(array,base_);}
    // Damage backing transaction: native C can empty/shrink an inactive list.
    // Keep B's original allocation private, just as its unchanged players are
    // leased. Native141764AC0 only resizes pointer storage, not player state or
    // evaluation; PreparePrivateTypes verifies its signature before publication.
    bool DamageBackingIntact(const Effect& e) const {
        if(e.damage_backing==DamageBackingPhase::Poisoned)return false;
        if(e.damage_backing!=DamageBackingPhase::Private && e.damage_backing!=DamageBackingPhase::Recovered)return true;
        const auto& a=e.damage_backing==DamageBackingPhase::Private?e.private_backing:*e.active;
        if(a.data!=e.active_image.data || a.count!=e.active_image.count || a.capacity!=e.active_image.capacity
            || !ValidArray(a,4) || a.capacity>16 || !a.data)return false;
        for(int j=0;j<a.count;++j)if(a.data[j]!=e.players[j].object)return false;
        return true;
    }
    Status ParkDamageBackings() const {
        // Complete acquisition preflight before detaching the first allocation.
        for(int i=0;i<pool_image_.count;++i) {
            const auto& e=effects_[i];if(!e.active_image.count)continue;
            if(e.damage_backing!=DamageBackingPhase::Attached || e.private_backing.data
                || e.active->data!=e.active_image.data || e.active->count!=e.active_image.count
                || e.active->capacity!=e.active_image.capacity || !ValidArray(*e.active,4)
                || e.active->capacity>16 || e.stopped->count)return Fail();
            for(int j=0;j<e.active_image.count;++j)if(e.active->data[j]!=e.players[j].object)return Fail();
            for(int j=0;j<i;++j) {
                const auto& prior=effects_[j];
                if(prior.active==e.active || (prior.active_image.count && prior.active_image.data==e.active_image.data))return Fail();
            }
        }
        for(int i=0;i<pool_image_.count;++i) {
            const auto& e=effects_[i];if(!e.active_image.count)continue;
            e.private_backing=*e.active;e.damage_backing=DamageBackingPhase::Private;
            *e.active={};
        }
        return Status::success();
    }
    Status RecoverDamageBacking(const Effect& e) const {
        if(!DamageBackingIntact(e))return Fail();
        if(e.damage_backing==DamageBackingPhase::Attached || e.damage_backing==DamageBackingPhase::Recovered)return Status::success();
        if(e.damage_backing!=DamageBackingPhase::Private || !ValidArray(*e.active,4)
            || e.active->count || e.stopped->count || e.active->capacity>16
            || e.active->data==e.private_backing.data)return Fail();
        // Only the validated empty C list can be retired here. A failed native
        // allocator postcondition poisons the operation; never free it twice.
        e.damage_backing=DamageBackingPhase::Poisoned;
        ResizeDamageBacking(*e.active);
        if(e.active->data || e.active->capacity)return Fail();
        *e.active=e.private_backing;e.private_backing={};
        e.damage_backing=DamageBackingPhase::Recovered;
        return Status::success();
    }
    bool FinishDamagePlayer(const Effect& e,int index) const {
        if(index<0 || index>=e.active_image.count || !Live(e.players[index]))return false;
        auto* player=e.players[index].object;
        if(At<std::uintptr_t>(player,0)!=base_+0x373b998 || At<Object*>(player,0x370)!=e.animation.object
            || (At<unsigned char>(player,0x761)&1) || At<int>(player,0x770))return false;
        if(!At<int>(player,0x6d8) && !At<int>(player,0x400) && !At<int>(player,0x410))return true;
        reinterpret_cast<void(*)(void*)>(base_+0x18010d0)(player);
        return !At<int>(player,0x6d8) && !At<int>(player,0x400) && !At<int>(player,0x410)
            && !At<int>(player,0x770) && !(At<unsigned char>(player,0x761)&1);
    }
    Status CommitDamageBackings() const {
        if(!ValidateBindings(false).ok())return Fail();
        for(int i=0;i<pool_image_.count;++i) {
            const auto& e=effects_[i];if(!e.active_image.count || e.damage_backing==DamageBackingPhase::Retired)continue;
            if((e.damage_backing!=DamageBackingPhase::Private && e.damage_backing!=DamageBackingPhase::Retiring) || !DamageBackingIntact(e))return Fail();
            e.damage_backing=DamageBackingPhase::Retiring;
            for(int j=0;j<e.active_image.count;++j)if(!e.evaluation_finished[j]) {
                if(!FinishDamagePlayer(e,j))return Fail();e.evaluation_finished[j]=true;
            }
            e.private_backing.count=0;
            ResizeDamageBacking(e.private_backing);
            if(e.private_backing.data || e.private_backing.capacity){e.damage_backing=DamageBackingPhase::Poisoned;return Fail();}
            e.damage_backing=DamageBackingPhase::Retired;
        }
        return Status::success();
    }
    // Both images remain alive. B players and their backing remain private
    // while C owns its native list. Undo returns B's original allocation.
    // Historical Slate pointers are witnesses, never restore addresses. Native
    // setters publish A values through validated current B backing; UObject
    // membership stays exact even if Slate materialized lazily between A/B.
    Status Install(const Sc6ReplayHudState& physical,Prepared& prepared,bool recover=false) const {
        if(!ValidateBindings(false,!recover).ok() || !physical.ValidateBindings().ok()) return Fail();
        if(damage_listener_!=physical.damage_listener_)return Fail();
        if(!SameTypes(physical))return Fail();
        if(recover && this==&physical) {
            // No captured C image: only untouched B or players explicitly
            // owned by the A publication journal may occupy the native list.
            // Never free an unknown C list merely because C capture failed.
            if(!prepared.target)return Fail();
            for(int i=0;i<type_pool_image_.count;++i) {
                const auto& e=type_effects_[i];
                for(int j=0;j<e.active->count;++j) {
                    auto* player=e.active->data[j];bool known=false;
                    if(e.active_image.count && player==e.players[0].object
                        && e.private_player.phase()!=ReplayHudPrivatePlayer::Phase::Private)known=true;
                    const auto& a=prepared.target->type_effects_[i];
                    if(a.active_image.count && player==prepared.type_players[i].identity.object
                        && prepared.players[i].phase()!=ReplayHudPlayerOperation::Phase::Empty
                        && prepared.players[i].phase()!=ReplayHudPlayerOperation::Phase::Prepared)known=true;
                    if(!known)return Fail();
                }
            }
        }
        for(int i=0;i<pool_image_.count;++i) {
            const auto& e=effects_[i];const auto& b=physical.effects_[i];
            if(!DamageBackingIntact(e))return Fail();
            if(recover && (e.damage_backing==DamageBackingPhase::Retired || e.damage_backing==DamageBackingPhase::Retiring))return Fail();
            if(e.widget!=b.widget || e.image!=b.image || e.slot!=b.slot || e.animation!=b.animation)return Fail();
            if(recover && e.damage_backing==DamageBackingPhase::Recovered)continue;
            if(recover && this==&physical && (e.damage_backing==DamageBackingPhase::Private
                || prepared.damage_players[i].identity.object)) {
                if(!ValidateDiscardedDamage(physical,prepared,i) || (e.private_backing.data && e.active->data==e.private_backing.data))return Fail();
                continue;
            }
            if(e.widget!=b.widget || e.image!=b.image || e.slot!=b.slot || e.animation!=b.animation
                || e.active->data!=b.active_image.data || e.active->capacity!=b.active_image.capacity
                || (recover && e.damage_backing!=DamageBackingPhase::Private && e.active_image.count>e.active->capacity)
                || e.stopped->data!=b.stopped_image.data || e.stopped->capacity!=b.stopped_image.capacity) {
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] HUD damage installation rejected recover={} effect={} widget={} image={} slot={} animation={} active_backing={} active_capacity={}/{} required={} stopped_backing={} stopped_capacity={}/{} before_write=true\n"),
                    recover,i,e.widget==b.widget,e.image==b.image,e.slot==b.slot,e.animation==b.animation,
                    e.active->data==b.active_image.data,e.active->capacity,b.active_image.capacity,e.active_image.count,
                    e.stopped->data==b.stopped_image.data,e.stopped->capacity,b.stopped_image.capacity);
                return Fail();
            }
            // Only captured C players or the explicit fresh-A journal may be
            // finished during undo; private B players must remain absent.
            if(recover && e.damage_backing==DamageBackingPhase::Private
                && !ValidateDiscardedDamage(physical,prepared,i))return Fail();
        }
        for(int i=0;i<type_pool_image_.count;++i) {
            const auto& e=type_effects_[i];const auto& b=physical.type_effects_[i];
            if(recover && this==&physical && prepared.type_backings[i]!=Prepared::TypeBacking::Empty
                && prepared.type_backings[i]!=Prepared::TypeBacking::Recovered) {
                if(e.active_image.data || e.active_image.count || e.active_image.capacity
                    || !ValidArray(*e.active,4) || e.active->capacity>16)return Fail();
                continue; // current members were checked against the A journal above
            }
            if(recover && this==&physical && e.private_player.phase()==ReplayHudPrivatePlayer::Phase::Private) {
                if(!ValidArray(*e.active,4) || e.active->capacity>16 || e.active->data==e.private_backing.data)return Fail();
                continue;
            }
            if(e.active->data!=b.active_image.data || e.active->capacity!=b.active_image.capacity
                || (recover && e.private_player.phase()!=ReplayHudPrivatePlayer::Phase::Private && e.active_image.count>e.active->capacity) || e.stopped->data!=b.stopped_image.data
                || e.stopped->capacity!=b.stopped_image.capacity)return Fail();
        }
        if(recover && !UndoDamagePlayers(physical,prepared))return Fail();
        // Retire discarded C evaluation before B widget values are installed.
        // This native Finish path does not dispatch UMG finished delegates.
        if(recover && !physical.FinishDiscardedTypes(this).ok())return Fail();
        if(recover && !UndoTypePlayers(physical,prepared).ok())return Fail();
        if(recover && !RecoverTypeBackings(prepared))return Fail();
        if(recover) {
            if(prepared.private_hud!=this || !physical.RetireAnnouncements(this).ok())return Fail();
        } else if(prepared.private_hud!=&physical || !physical.ParkAnnouncements().ok()
            || !physical.ParkPrivateTypes().ok() || !physical.ParkDamageBackings().ok())return Fail();
        std::memcpy(&At<unsigned char>(damage_listener_.object,0x3a8),damage_latches_.data(),damage_latches_.size());
        for(unsigned i=0;i<7;++i) *addresses_[i]=values_[i];
        for(unsigned i=0;i<2;++i) *reference_addresses_[i]=references_[i].object;
        for(int i=0;i<pool_image_.count;++i) {
            const auto& e=effects_[i];
            if(recover && !RecoverDamageBacking(e).ok())return Fail();
            // Native C may reuse or reallocate this widget's array. B player
            // objects and their complete unchanged images are independently
            // leased/validated above. Restore membership through the validated
            // current backing; never copy an old allocation address or a raw
            // animation-player image. No Stop/Play callback is dispatched.
            if(recover) {
                for(int j=0;j<e.active_image.count;++j)e.active->data[j]=e.players[j].object;
                e.active->count=e.active_image.count;
            } else {e.active->count=0;}
            reinterpret_cast<void(*)(void*,const void*)>(base_+0x18180b0)(e.image.object,e.color.data());
            reinterpret_cast<void(*)(void*,const void*)>(base_+0x181b640)(e.slot.object,e.offsets.data());
            bool attached{};if(!Attached(e,attached)) return Fail();
            const auto transform=PresentationTransform(e,attached);
            reinterpret_cast<void(*)(void*,const void*)>(base_+0x181c380)(e.image.object,transform.data());
            // Recompute native Slate volatility from the restored active list;
            // unlike Stop/Play this does not evaluate or dispatch animation events.
            reinterpret_cast<void(*)(void*)>(base_+0x17f3f30)(e.widget.object);
        }
        if(!recover && !PublishDamagePlayers(prepared))return Fail();
        if(!InstallTypeValues(recover,recover).ok())return Fail();
        if(!recover && !PublishTypePlayers(prepared).ok())return Fail();
        // Native1403FBC10 runs every actor tick and republishes +388 through
        // the widget visibility virtual. Restoring only the widget value is
        // overwritten on the next actor tick. Preserve that logical request
        // with the same retained actor identity, including complete B undo.
        reinterpret_cast<void(*)(void*,unsigned char)>(base_+0x3fa280)(cockpit_actor_.object,cockpit_requested_visibility_);
        // Factory1403EC740 consumes this enable before creating future native
        // round announcements. It belongs to A/B publication, not cached pixels.
        reinterpret_cast<void(*)(void*,unsigned char)>(base_+0x3fa280)(announce_actor_.object,announce_enabled_);
        // Endpoint playback collapses this retained root. Publish the captured
        // visibility through native current-Slate reconstruction, after child
        // state is coherent. The same path restores B on cancellation; no old
        // Slate pointer or Blueprint SetCockpitVisible side effects are used.
        // CaptureTypes verifies this native setter's signature before admission.
        reinterpret_cast<void(*)(void*,unsigned char)>(base_+0x181e1b0)(cockpit_.object,cockpit_visibility_);
        if(recover && !RecoverAnnouncements().ok())return Fail();
        return ValidateValues(recover?nullptr:&prepared);
    }
};
}
