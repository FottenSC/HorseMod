#pragma once

#include <polyhook2/Detour/x64Detour.hpp>
#include <memory>
#include <intrin.h>
#include <mutex>
#include <atomic>
#include "ReplayIndexCompletion.hpp"
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>

// Test-only observation of native callees, shared by stock and replacement
// execution. No executor counters or expected replay state enter this path.
class ReplayBoundaryObserver
{
public:
    using Sink = void (*)(void*, const wchar_t*);
    using ActorSink = void (*)(void*, void*, std::uintptr_t);
    using ConsumerMutationQuery = bool (*)(void*,void**,void**);
    bool ArmConsumerMutation(ConsumerMutationQuery query) {
        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        constexpr unsigned char signature[]{0x48,0x85,0xd2,0x74,0x13,0x4c,0x8d,0x82,0x10,1,0,0,0x48,0x81,0xc1,0x10};
        if(!query || GetCurrentThreadId()!=thread_ || consumer_query_ || consumer_injected_
            || std::memcmp(reinterpret_cast<void*>(base+0x1d58ed0),signature,sizeof signature))return false;
        consumer_query_=query;return true;
    }
    bool ConsumerMutationInjected()const noexcept {return consumer_injected_;}
    int LastInputFramesBack()const noexcept {return input_frames_back_;}

    bool Start(void* manager, void* context, Sink sink, ActorSink actor_sink, bool changed_inputs = false, bool corrected_inputs = false, bool defer_inputs = false, bool observe_hud_clock = false, unsigned hud_target = 0, bool index_sequence = false)
    {
        error_ = "boundary_observer_invalid_owner";
        if (active_ != nullptr || manager == nullptr || sink == nullptr) return false;
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        constexpr std::array<unsigned char, 12> callback_signature{
            0x40, 0x56, 0x48, 0x83, 0xec, 0x20, 0xff, 0x41, 0x64, 0x48, 0x8b, 0xf1};
        constexpr std::array<unsigned char, 12> round_signature{
            0x4c, 0x8b, 0xdc, 0x55, 0x41, 0x54, 0x41, 0x55, 0x49, 0x8d, 0x6b, 0xc8};
        constexpr std::array<unsigned char, 15> tail_signature{
            0x40, 0x55, 0x53, 0x48, 0x8d, 0x6c, 0x24, 0xb1, 0x48, 0x81, 0xec, 0x88, 0, 0, 0};
        error_ = "boundary_observer_callback_signature";
        if (std::memcmp(reinterpret_cast<void*>(base + 0x3999c0), callback_signature.data(), callback_signature.size())) return false;
        error_ = "boundary_observer_round_signature";
        if (std::memcmp(reinterpret_cast<void*>(base + 0x3fce80), round_signature.data(), round_signature.size())) return false;
        error_ = "boundary_observer_tail_signature";
        if (std::memcmp(reinterpret_cast<void*>(base + 0x1c2d070), tail_signature.data(), tail_signature.size())) return false;
        constexpr std::array<unsigned char, 15> task_signature{
            0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20};
        error_ = "boundary_observer_task_signature";
        if (std::memcmp(reinterpret_cast<void*>(base + 0x215ed20), task_signature.data(), task_signature.size())) return false;
        constexpr std::array<unsigned char, 15> input_signature{
            0x48,0x89,0x74,0x24,0x20,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x20};
        error_ = "boundary_observer_input_signature";
        if (std::memcmp(reinterpret_cast<void*>(base + 0x3fcd10), input_signature.data(), input_signature.size())) return false;
        constexpr unsigned char cache_signature[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
        if(changed_inputs && std::memcmp(reinterpret_cast<void*>(base+0x3f0720),cache_signature,sizeof(cache_signature))) return false;
        changed_inputs_=changed_inputs;changed_reads_=different_reads_=0;
        first_changed_sample_=corrected_inputs?161u:166u;inputs_active_=!defer_inputs;
        input_policy_=corrected_inputs?L"p0_raw_cache_161_190_v1":L"p0_raw_cache_166_195_v1";
        manager_ = reinterpret_cast<std::uintptr_t>(manager);
        world_ = ActorWorld(manager);
        error_ = "boundary_observer_world_binding";
        if (!world_) { manager_ = 0; return false; }
        thread_ = GetCurrentThreadId();
        context_ = context;
        sink_ = sink;
        actor_sink_ = actor_sink;
        task_family_count_ = 0;
        component_family_count_ = 0;
        cri_census_tick_ = 0;
        next_input_slot_ = 0;
        task_failed_.store(false);
        active_ = this;
        callbacks_ = std::make_unique<PLH::x64Detour>(base + 0x3999c0,
            reinterpret_cast<std::uint64_t>(&Callbacks), &callback_original_);
        round_ = std::make_unique<PLH::x64Detour>(base + 0x3fce80,
            reinterpret_cast<std::uint64_t>(&RoundSequence), &round_original_);
        tail_ = std::make_unique<PLH::x64Detour>(base + 0x1c2d070,
            reinterpret_cast<std::uint64_t>(&ActorTail), &tail_original_);
        task_ = std::make_unique<PLH::x64Detour>(base + 0x215ed20,
            reinterpret_cast<std::uint64_t>(&TickTask), &task_original_);
        input_ = std::make_unique<PLH::x64Detour>(base + 0x3fcd10,
            reinterpret_cast<std::uint64_t>(&InputCachePublication), &input_original_);
        error_ = "boundary_observer_callback_install";
        if (!callbacks_->hook()) { Stop(); return false; }
        error_ = "boundary_observer_round_install";
        if (!round_->hook()) { Stop(); return false; }
        error_ = "boundary_observer_tail_install";
        if (!tail_->hook()) { Stop(); return false; }
        error_ = "boundary_observer_task_install";
        if (!task_->hook()) { Stop(); return false; }
        error_ = "boundary_observer_input_install";
        if (!input_->hook()) { Stop(); return false; }
        if(observe_hud_clock) {
            constexpr unsigned char signature[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x70};
            error_="hud_clock_signature";
            if(std::memcmp(reinterpret_cast<void*>(base+0x17f6d70),signature,sizeof(signature))) {Stop();return false;}
            hud_clock_rows_=0;hud_target_=hud_target;hud_state_rows_=0;index_sequence_=index_sequence;
            hud_root_=nullptr;hud_input_tick_=hud_input_rows_=0;
            if(index_sequence_)for(auto target:ReplayQualification::index_observation_targets)
                RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] HUD observation window target={} first={} last={} read_only=true\n"),target,target-16,target+120);
            if(!index_sequence_ && hud_target_)RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] HUD observation window target={} first={} last={} read_only=true\n"),hud_target_,hud_target_-16,hud_target_+120);
            hud_clock_=std::make_unique<PLH::x64Detour>(base+0x17f6d70,reinterpret_cast<std::uint64_t>(&ObserveHudClock),&hud_clock_original_);
            error_="hud_clock_install";
            if(!hud_clock_->hook()) {Stop();return false;}
            constexpr unsigned char visible_signature[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x30,0x48,0x8d,0x54,0x24,0x20};
            constexpr unsigned char health_signature[]{0x48,0x89,0x5c,0x24,0x10,0x56,0x48,0x83,0xec,0x30,0x41,0x8b,0xd8,0x48,0x8b,0xf2};
            error_="hud_consumed_input_signature";
            if(std::memcmp(reinterpret_cast<void*>(base+0x17f4ed0),visible_signature,sizeof(visible_signature))
                || std::memcmp(reinterpret_cast<void*>(base+0x3c1200),health_signature,sizeof(health_signature))) {Stop();return false;}
            hud_visible_=std::make_unique<PLH::x64Detour>(base+0x17f4ed0,reinterpret_cast<std::uint64_t>(&ObserveHudVisible),&hud_visible_original_);
            hud_health_=std::make_unique<PLH::x64Detour>(base+0x3c1200,reinterpret_cast<std::uint64_t>(&ObserveHudHealth),&hud_health_original_);
            error_="hud_consumed_input_install";
            if(!hud_visible_->hook() || !hud_health_->hook()) {Stop();return false;}
        }
        if(changed_inputs_) {
            cache_ = std::make_unique<PLH::x64Detour>(base+0x3f0720,reinterpret_cast<std::uint64_t>(&ReadChangedInput),&cache_original_);
            error_="changed_input_cache_install";
            if(!cache_->hook()) {Stop();return false;}
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] changed input started policy={} round=0 first_sample={} last_sample={} deferred={}\n"),input_policy_,first_changed_sample_,first_changed_sample_+29,defer_inputs);
        }
        error_ = "none";
        return true;
    }

    bool ActivateChangedInputs() {
        if(!changed_inputs_ || inputs_active_ || GetCurrentThreadId()!=thread_ || changed_reads_) return false;
        inputs_active_=true;
        RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] changed input activated policy={} after_restore=true\n"),input_policy_);
        return true;
    }
    const char* error() const { return error_; }
    bool healthy() const { return !task_failed_.load(); }

    bool Stop()
    {
        // Installed and removed at EngineTickPost, outside the observed
        // manager's game-thread calls. Other callback collections only forward.
        manager_ = 0;
        const bool input_complete = next_input_slot_ == 0;
        bool ok = true;
        if(hud_health_ && hud_health_->isHooked()) ok=hud_health_->unHook() && ok;
        if(hud_visible_ && hud_visible_->isHooked()) ok=hud_visible_->unHook() && ok;
        if(hud_clock_ && hud_clock_->isHooked()) ok=hud_clock_->unHook() && ok;
        if (cache_ && cache_->isHooked()) ok = cache_->unHook() && ok;
        if (input_ && input_->isHooked()) ok = input_->unHook() && ok;
        if (task_ && task_->isHooked()) ok = task_->unHook() && ok;
        if (tail_ && tail_->isHooked()) ok = tail_->unHook() && ok;
        if (round_ && round_->isHooked()) ok = round_->unHook() && ok;
        if (callbacks_ && callbacks_->isHooked()) ok = callbacks_->unHook() && ok;
        if (ok)
        {
            if(hud_clock_) RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] HUD clock observation completed rows={} read_only=true detached=true\n"),hud_clock_rows_);
            hud_clock_.reset();
            hud_visible_.reset();hud_health_.reset();hud_root_=nullptr;
            if(cache_) RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] changed input completed policy={} reads={} differing_reads={} failed={} detached=true\n"),input_policy_,changed_reads_,different_reads_,task_failed_.load());
            cache_.reset();
            round_.reset();
            tail_.reset();
            callbacks_.reset();
            task_.reset();
            input_.reset();
            active_ = nullptr;
        }
        return ok && input_complete;
    }

    ~ReplayBoundaryObserver() { Stop(); }

private:
    // Bounded causal probe. Record only native calls made by Cockpit Tick;
    // never issue another IsVisible call (its bound attribute may evaluate).
    static bool ObserveHudVisible(void* widget) {
        auto& self=*active_;
        const auto result=reinterpret_cast<bool(*)(void*)>(self.hud_visible_original_)(widget);
        if(GetCurrentThreadId()==self.thread_ && self.hud_root_) {
            auto** main=self.hud_root_->GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>(L"MAIN");
            if(main && *main==widget && self.hud_input_rows_++<256)
                RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] HUD consumed visible tick={} value={} forwarded_once=true\n"),self.hud_input_tick_,result);
        }
        return result;
    }
    static void ObserveHudHealth(void* context,void* output,int player) {
        auto& self=*active_;
        reinterpret_cast<void(*)(void*,void*,int)>(self.hud_health_original_)(context,output,player);
        if(GetCurrentThreadId()==self.thread_ && self.hud_root_==context && self.hud_root_ && output && self.hud_input_rows_++<256) {
            std::array<unsigned,2> value{};std::memcpy(value.data(),output,8);
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] HUD consumed health tick={} player={} value={:08x},{:08x} forwarded_once=true\n"),self.hud_input_tick_,player,value[0],value[1]);
        }
    }
    // Temporary causal observation for the active-checkpoint ownership plan.
    // Native arguments and player state are forwarded untouched. No new image
    // capture or clock intervention; through230 covers the first ten widget
    // updates after the failed B220 preparation's independently checked recovery.
    static void ObserveHudClock(void* widget,void* geometry,float delta) {
        auto& self=*active_;
        const auto original=reinterpret_cast<void(*)(void*,void*,float)>(self.hud_clock_original_);
        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const auto tick=*reinterpret_cast<const unsigned*>(base+0x470d0c4);
        const bool target_window=(self.index_sequence_ && ReplayQualification::ObserveIndexSequenceTick(tick))
            || ReplayQualification::ObserveEarlyCombatTarget(tick,self.hud_target_)
            || (self.hud_target_ && tick>=self.hud_target_-16 && tick<=self.hud_target_+120);
        const bool private_hud_window=tick>=300 && tick<=449;
        const bool observed= (tick>=205 && tick<=230) || (tick>=2504 && tick<=2624) || target_window || private_hud_window;
        if(!self.manager_ || GetCurrentThreadId()!=self.thread_ || !observed) {original(widget,geometry,delta);return;}
        auto* object=static_cast<RC::Unreal::UObject*>(widget);
        const auto name=object->GetClassPrivate()->GetName();
        if((tick>=205 && tick<=230) || (tick>2504 && tick<=2514) || target_window || private_hud_window) {
            bool cockpit=false;
            for(auto* type=static_cast<RC::Unreal::UStruct*>(object->GetClassPrivate());type;type=type->GetSuperStruct())
                if(type->GetName()==L"CockpitBase_C") {cockpit=true;break;}
            if(cockpit) {
                const bool causal=tick>=205 && tick<=230;
                const auto previous=self.hud_root_;
                if(causal) {self.hud_root_=object;self.hud_input_tick_=tick;}
                original(widget,geometry,delta);
                self.hud_root_=previous;
                if(causal) {
                    auto* starting=RC::Unreal::CastField<RC::Unreal::FBoolProperty>(object->GetPropertyByNameInChain(L"IsBattleStarting"));
                    if(!starting){self.task_failed_=true;return;}
                    RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] HUD consumed starting tick={} value={} read_only=true\n"),tick,starting->GetPropertyValueInContainer(object));
                }
                constexpr const wchar_t* fields[]{L"PrevP1VitalPer",L"PrevP2VitalPer",L"PrevComboP1VitalPer",L"PrevComboP2VitalPer",L"StartingTimer",L"DmgEffCount",L"TypeEffCount"};
                std::array<unsigned,7> values{};
                for(unsigned i=0;i<values.size();++i) {
                    const auto* value=object->GetValuePtrByPropertyNameInChain<unsigned>(fields[i]);
                    if(!value){self.task_failed_=true;return;} values[i]=*value;
                }
                struct Pool {RC::Unreal::UObject** data;int count,capacity;};
                const auto* pool=object->GetValuePtrByPropertyNameInChain<Pool>(L"DmgEff");
                if(!pool || pool->count<1 || pool->count>16 || pool->capacity<pool->count || !pool->data){self.task_failed_=true;return;}
                unsigned active=0;
                for(int i=0;i<pool->count;++i) {
                    auto* effect=pool->data[i];
                    const auto* item=effect?RC::Unreal::FUObjectArray::IndexToObject(effect->GetInternalIndex()):nullptr;
                    if(!item || item->GetUObject()!=effect || !item->IsValid(false)){self.task_failed_=true;return;}
                    const auto* players=effect->GetValuePtrByPropertyNameInChain<Pool>(L"ActiveSequencePlayers");
                    if(!players || players->count<0 || players->count>4 || players->capacity<players->count
                        || (players->count && !players->data)){self.task_failed_=true;return;}
                    active+=players->count;

                }
                const auto* type_pool=object->GetValuePtrByPropertyNameInChain<Pool>(L"DmgTypeEff");
                if(!type_pool || type_pool->count<1 || type_pool->count>16 || type_pool->capacity<type_pool->count || !type_pool->data){self.task_failed_=true;return;}
                unsigned type_active=0;std::uint64_t type_players=14695981039346656037ull;
                const auto hash=[&](const void* data,std::size_t size) {
                    const auto* bytes=static_cast<const unsigned char*>(data);
                    for(std::size_t n=0;n<size;++n){type_players^=bytes[n];type_players*=1099511628211ull;}
                };
                for(int i=0;i<type_pool->count;++i) {
                    auto* effect=type_pool->data[i];
                    const auto* item=effect?RC::Unreal::FUObjectArray::IndexToObject(effect->GetInternalIndex()):nullptr;
                    if(!item || item->GetUObject()!=effect || !item->IsValid(false)){self.task_failed_=true;return;}
                    const auto* players=effect->GetValuePtrByPropertyNameInChain<Pool>(L"ActiveSequencePlayers");
                    if(!players || players->count<0 || players->count>4 || players->capacity<players->count
                        || (players->count && !players->data)){self.task_failed_=true;return;}
                    type_active+=players->count;
                    hash(&i,sizeof(i));hash(&players->count,sizeof(players->count));
                    for(int j=0;j<players->count;++j) {
                        auto* player=players->data[j];
                        const auto* live=player?RC::Unreal::FUObjectArray::IndexToObject(player->GetInternalIndex()):nullptr;
                        if(!live || live->GetUObject()!=player || !live->IsValid(false)
                            || *reinterpret_cast<const std::uintptr_t*>(player)!=base+0x373b998){self.task_failed_=true;return;}
                        const auto* bytes=reinterpret_cast<const std::byte*>(player);
                        // Logical values only: no pointers, padding, expected
                        // observations or independent-run allocation layout.
                        for(auto offset:{0x6a0,0x6a8,0x6b0,0x6c0,0x6d0})hash(bytes+offset,8);
                        for(auto offset:{0x6d8,0x750,0x754,0x758,0x75c})hash(bytes+offset,4);
                        for(auto offset:{0x6b8,0x6c8,0x760})hash(bytes+offset,1);
                    }
                }
                // Observe real native HUD fields and explicit inactive-player
                // coverage. An absent damage-clock callback is not evidence.
                // A sequence includes the indexed pass and up to five restored traversals.
                // Keep a finite streamed diagnostic allowance; no observations are dropped.
                if(++self.hud_state_rows_>((self.index_sequence_ || (self.hud_target_>=210 && self.hud_target_<=816))?8192u:1024u)){self.task_failed_=true;return;}
                RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] HUD state tick={} p1={:08x} p2={:08x} combo1={:08x} combo2={:08x} timer={:08x} damage={} types={} pool={} active={} type_pool={} type_active={} type_players={:016x} read_only=true\n"),
                    tick,values[0],values[1],values[2],values[3],values[4],values[5],values[6],pool->count,active,type_pool->count,type_active,type_players);
                return;
            }
        }
        // The B300 experiment compares complete logical HUD observations,
        // including native completion. Do not apply the short early-window
        // constant-clock diagnostic to animations that naturally end here.
        if((private_hud_window && !target_window) || (name!=L"DmgValueEff_C" && name!=L"DmgTypeEff_C")) {original(widget,geometry,delta);return;}
        struct Array {void** data;int count,capacity;};
        const auto list=*reinterpret_cast<const Array*>(static_cast<const std::byte*>(widget)+0x190);
        if(list.count<0 || list.count>4 || list.capacity<list.count || (list.count && !list.data)) {
            self.task_failed_.store(true);original(widget,geometry,delta);return;
        }
        std::array<void*,4> players{};std::array<double,4> times{};
        std::array<int,4> indices{},serials{};
        for(int i=0;i<list.count;++i) {
            players[i]=list.data[i];
            if(!players[i] || *static_cast<const std::uintptr_t*>(players[i])!=base+0x373b998) {
                self.task_failed_.store(true);original(widget,geometry,delta);return;
            }
            indices[i]=static_cast<RC::Unreal::UObject*>(players[i])->GetInternalIndex();
            const auto* item=RC::Unreal::FUObjectArray::IndexToObject(indices[i]);
            if(!item || item->GetUObject()!=players[i] || !item->IsValid(false)) {
                self.task_failed_.store(true);original(widget,geometry,delta);return;
            }
            serials[i]=item->GetSerialNumber();
            times[i]=*reinterpret_cast<const double*>(static_cast<const std::byte*>(players[i])+0x6a0);
        }
        const auto* slate=*reinterpret_cast<const std::byte* const*>(base+0x42a8dd8);
        const auto slate_delta=slate?*reinterpret_cast<const double*>(slate+0x518)-*reinterpret_cast<const double*>(slate+0x520):0.0;
        original(widget,geometry,delta);
        // Recheck lifetime after native callbacks; never dereference a player
        // merely because it appeared in the pre-call active array.
        for(int i=0;i<list.count;++i) {
            if(++self.hud_clock_rows_>((self.index_sequence_ || (self.hud_target_>=210 && self.hud_target_<=816))?16384u:2048u)) {self.task_failed_.store(true);return;}
            const auto* item=RC::Unreal::FUObjectArray::IndexToObject(indices[i]);
            if(!item || item->GetUObject()!=players[i] || !item->IsValid(false) || item->GetSerialNumber()!=serials[i]) {
                self.task_failed_.store(true);return;
            }
            const auto after=*reinterpret_cast<const double*>(static_cast<const std::byte*>(players[i])+0x6a0);
            RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] HUD clock tick={} widget={} player={} delta={:.9f} slate_delta={:.9f} before={:.9f} after={:.9f} read_only=true\n"),
                tick,name,i,delta,slate_delta,times[i],after);
        }
    }
    // Test-only scripted input source. Native1403F0720 validates the row;
    // native1403FCD10 remains responsible for prior/held/rising and masks.
    // No recorded array, cache cell, expected observation or output state is written.
    static std::uint32_t ReadChangedInput(void* log,unsigned slot,int round,unsigned frame) {
        auto* self=active_;
        const auto original=reinterpret_cast<std::uint32_t(*)(void*,unsigned,int,unsigned)>(self->cache_original_)(log,slot,round,frame);
        if(!self->manager_ || !self->inputs_active_ || GetCurrentThreadId()!=self->thread_ || slot!=0 || round!=0 || frame<self->first_changed_sample_ || frame>self->first_changed_sample_+29
            || log!=*reinterpret_cast<void**>(self->manager_+0x478)) return original;
        const auto* bytes=static_cast<const std::byte*>(log);
        const auto* cell=bytes+0x3c0+(frame&511)*16;
        if(*reinterpret_cast<const int*>(bytes+0x390)!=0 || *reinterpret_cast<const int*>(cell)!=round
            || *reinterpret_cast<const unsigned*>(cell+4)!=frame || *reinterpret_cast<const unsigned*>(cell+8)!=original) {
            self->task_failed_=true;return original;
        }
        const auto input=(original&~0x3c0fu)|(frame<self->first_changed_sample_+15 ? 0x401u : 0u);
        ++self->changed_reads_;self->different_reads_+=input!=original;
        RC::Output::send<RC::LogLevel::Default>(STR("[ReplayQualification] changed input sample policy={} round={} sample={} slot={} original={:08x} input={:08x}\n"),self->input_policy_,round,frame,slot,original,input);
        return input;
    }

    static void InputCachePublication(void* manager, std::uint32_t slot, int frames_back,
        std::uint8_t suppress_rising, std::uint8_t suppress_held)
    {
        auto* self = active_;
        reinterpret_cast<void (*)(void*, std::uint32_t, int, std::uint8_t, std::uint8_t)>(
            self->input_original_)(manager, slot, frames_back, suppress_rising, suppress_held);
        if (GetCurrentThreadId() != self->thread_ || !self->manager_
            || reinterpret_cast<std::uintptr_t>(manager) != self->manager_) return;
        const auto count = *reinterpret_cast<std::int32_t*>(self->manager_ + 0x14b0);
        if (count < 1 || count > 2 || slot >= static_cast<std::uint32_t>(count)
            || slot != self->next_input_slot_
            || (slot && frames_back != self->input_frames_back_))
        { self->task_failed_.store(true); return; }
        self->input_frames_back_ = frames_back;
        ++self->next_input_slot_;
        // 1403FE520 calls this once per slot for each consumed input sample.
        // Repeated simulation ticks bypass it. Observe the completed pair set
        // after the last slot, before the separate input-filter callback.
        if (slot + 1 == static_cast<std::uint32_t>(count))
        {
            self->next_input_slot_ = 0;
            self->sink_(self->context_, L"input_cache_publication");
        }
    }

    static void TickTask(void* task, void* new_tasks, std::uint32_t current_thread)
    {
        auto* self = active_;
        // Outside task_mutex_. Production independently checks the frozen
        // dependent task at the real native removal entry.
        if(GetCurrentThreadId()==self->thread_ && self->consumer_query_ && !self->consumer_injected_) {
            void* mesh{};void* source{};
            if(self->consumer_query_(task,&mesh,&source)) {
                self->consumer_injected_=true;
                const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
                reinterpret_cast<void(*)(void*,void*)>(base+0x1d58ed0)(mesh,source);
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayQualification] consumer prerequisite mutation injected count=1 native_api=141D58ED0 production_entry_returned=true repair=false\n"));
            }
        }
        // Inspect before forwarding: native 14215ED20 signals completion and
        // returns this task storage to its TLS pool before it returns.
        auto* bytes = static_cast<std::byte*>(task);
        if (*reinterpret_cast<void**>(bytes + 0x28) == self->world_)
        {
            const auto function = *reinterpret_cast<std::uintptr_t*>(bytes + 0x10);
            const auto vtable = *reinterpret_cast<std::uintptr_t*>(function);
            const auto execute = *reinterpret_cast<std::uintptr_t*>(vtable + 8);
            const auto group = *reinterpret_cast<std::uint8_t*>(function + 10);
            const auto flags = *reinterpret_cast<std::uint8_t*>(function + 12);
            const bool game_thread = GetCurrentThreadId() == self->thread_;
            std::lock_guard lock(self->task_mutex_);
            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            const auto native_tick = *reinterpret_cast<const unsigned*>(base + 0x470d0c4);
            // Bounded, diagnostic-only census of concrete owners actually
            // dispatched in the short G1 window. Existing task-family rows
            // identify a shared tick wrapper, not an owner class/lifetime.
            if (vtable == base + 0x3865f98 && native_tick >= 205 && native_tick <= 224) {
                ComponentFamily component{};
                if (!InspectComponent(function, component)) self->task_failed_.store(true);
                else {
                    if (game_thread && component.table == base + 0x3360ca8
                        && self->cri_census_tick_ != native_tick) {
                        self->cri_census_tick_ = native_tick;
                        self->ObserveCriCallbacks(base, native_tick);
                    }
                    bool seen=false;
                    for(std::size_t i=0;i<self->component_family_count_;++i)
                        if(self->component_families_[i]==component) {seen=true;break;}
                    if(!seen) {
                        if(self->component_family_count_==self->component_families_.size()) self->task_failed_.store(true);
                        else {
                            self->component_families_[self->component_family_count_++]=component;
                            if(game_thread && component.table==base+0x3360ca8) {
                                std::array<ComponentFamily,2> roots{};
                                if(!InspectTraceRoots(component.object,roots))self->task_failed_.store(true);
                                else for(std::size_t i=0;i<roots.size();++i) {
                                    const auto& root=roots[i];
                                    RC::Output::send<RC::LogLevel::Default>(STR(
                                        "[ReplayQualification] trace retained root native_tick={} component_index={} role={} owner_index={} owner_serial={} class_index={} class_serial={} class={} owner_table_rva={:x} destructor_rva={:x} diagnostic_only=true\n"),
                                        native_tick,component.owner_index,i==0?STR("chara"):STR("scene"),
                                        root.owner_index,root.owner_serial,root.class_index,root.class_serial,
                                        reinterpret_cast<RC::Unreal::UObject*>(root.type)->GetFullName(),root.table-base,root.destroy-base);
                                }
                            }
                            RC::Output::send<RC::LogLevel::Default>(STR(
                                "[ReplayQualification] component task owner native_tick={} owner_index={} owner_serial={} class_index={} class_serial={} class={} owner_table_rva={:x} destructor_rva={:x} consumer_rva={:x} query_rva={:x} group={} flags={:x} game_thread={} task_thread={} diagnostic_only=true\n"),
                                native_tick,component.owner_index,component.owner_serial,component.class_index,component.class_serial,
                                reinterpret_cast<RC::Unreal::UObject*>(component.type)->GetFullName(),
                                component.table-base,component.destroy-base,component.consume-base,component.query-base,
                                group,flags,game_thread,current_thread);
                        }
                    }
                }
            }
            const TaskFamily family{execute, current_thread, group, flags, game_thread};
            bool known = false;
            for (std::size_t i = 0; i < self->task_family_count_; ++i)
                if (self->task_families_[i] == family) { known = true; break; }
            if (!known)
            {
                if (self->task_family_count_ == self->task_families_.size()) self->task_failed_.store(true);
                else
                {
                    self->task_families_[self->task_family_count_++] = family;
                    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
                    RC::Output::send<RC::LogLevel::Default>(STR(
                        "[ReplayQualification] tick family execute_rva={:x} group={} flags={:x} game_thread={} task_thread={}\n"),
                        execute - base, group, flags, game_thread, current_thread);
                }
            }
        }
        reinterpret_cast<void (*)(void*, void*, std::uint32_t)>(self->task_original_)(task, new_tasks, current_thread);
    }

    static void* ActorWorld(void* actor) noexcept
    {
        __try
        {
            const auto vtable = *reinterpret_cast<std::uintptr_t*>(actor);
            return reinterpret_cast<void* (*)(void*)>(
                *reinterpret_cast<std::uintptr_t*>(vtable + 0x138))(actor);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    static void Callbacks(void* collection)
    {
        // 1403999C0 has ONE argument. Unlike 141D38300, it does not
        // preserve a payload across its virtual callback dispatches.
        auto* self = active_;
        reinterpret_cast<void (*)(void*)>(self->callback_original_)(collection);
        if (GetCurrentThreadId() != self->thread_ || self->manager_ == 0) return;
        const auto offset = reinterpret_cast<std::uintptr_t>(collection) - self->manager_;
        const wchar_t* event = nullptr;
        switch (offset)
        {
        case 0x720: event = L"callback_720"; break;
        case 0x870: event = L"callback_870"; break;
        case 0x8e0: event = L"callback_8e0"; break;
        case 0x950: event = L"callback_950"; break;
        case 0xa30: event = L"callback_a30"; break;
        case 0xb80: event = L"callback_b80"; break;
        case 0xf70: event = L"callback_f70"; break;
        }
        if (event) self->sink_(self->context_, event);
    }

    static void RoundSequence(void* manager)
    {
        auto* self = active_;
        reinterpret_cast<void (*)(void*)>(self->round_original_)(manager);
        if (GetCurrentThreadId() == self->thread_ && self->manager_ != 0
            && reinterpret_cast<std::uintptr_t>(manager) == self->manager_)
            self->sink_(self->context_, L"round_sequence");
    }

    static void ActorTail(void* actor, float delta)
    {
        // 140562320 JMPs here. Full assembly consumes RCX/XMM1 only;
        // ReceiveTick and latent actions are mandatory outer-interval work.
        auto* self = active_;
        reinterpret_cast<void (*)(void*, float)>(self->tail_original_)(actor, delta);
        if (GetCurrentThreadId() == self->thread_ && self->manager_ != 0)
        {
            if (reinterpret_cast<std::uintptr_t>(actor) == self->manager_)
                self->sink_(self->context_, L"actor_tail");
            if (self->actor_sink_ && ActorWorld(actor) == self->world_)
                self->actor_sink_(self->context_, actor, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
        }
    }

    inline static ReplayBoundaryObserver* active_{};
    std::uintptr_t manager_{};
    void* world_{};
    DWORD thread_{};
    ConsumerMutationQuery consumer_query_{};
    bool consumer_injected_{};
    void* context_{};
    Sink sink_{};
    ActorSink actor_sink_{};
    const char* error_{"none"};
    std::uint64_t callback_original_{}, round_original_{}, tail_original_{};
    std::uint64_t task_original_{}, input_original_{}, cache_original_{};
    std::unique_ptr<PLH::x64Detour> cache_;
    std::unique_ptr<PLH::x64Detour> hud_clock_;
    std::unique_ptr<PLH::x64Detour> hud_visible_,hud_health_;
    std::uint64_t hud_visible_original_{},hud_health_original_{};
    RC::Unreal::UObject* hud_root_{};unsigned hud_input_tick_{},hud_input_rows_{};
    bool index_sequence_{};
    std::uint64_t hud_clock_original_{};unsigned hud_clock_rows_{},hud_target_{},hud_state_rows_{};
    bool changed_inputs_{},inputs_active_{};unsigned changed_reads_{},different_reads_{},first_changed_sample_{};
    const wchar_t* input_policy_{};
    std::uint32_t next_input_slot_{};
    int input_frames_back_{};
    struct TaskFamily
    {
        std::uintptr_t execute{};
        std::uint32_t task_thread{};
        std::uint8_t group{}, flags{};
        bool game_thread{};
        friend bool operator==(const TaskFamily&, const TaskFamily&) = default;
    };
    struct ComponentFamily {
        std::uintptr_t object{},type{},table{},destroy{},consume{},query{};
        std::int32_t owner_index{},owner_serial{},class_index{},class_serial{};
        friend bool operator==(const ComponentFamily&,const ComponentFamily&)=default;
    };
    struct CriCallbackSnapshot {
        struct Entry {
            std::uint32_t slot{};
            std::uintptr_t binding{}, table{}, resolver{};
            std::array<unsigned char, 0x70> bytes{};
        };
        std::uintptr_t manager{};
        std::int32_t slots{}, free{}, bits{}, capacity{};
        std::size_t count{};
        std::array<Entry, 128> entries{};
    };
    // Read-only diagnostic of the actual 140544470 sparse callback table.
    // Never wait while holding task_mutex_, allocate under the native lock,
    // call a context resolver, or retain any native pointer past this sample.
    // This is a snapshot, not a lifetime lease or writer-exclusion proof.
    static bool InspectCriCallbacks(std::uintptr_t base, CriCallbackSnapshot& out) noexcept {
        CRITICAL_SECTION* lock{};
        bool locked{};
        bool valid{};
        __try {
            __try {
                out.manager = *reinterpret_cast<std::uintptr_t*>(base + 0x41492e8);
                if (out.manager) {
                    lock = reinterpret_cast<CRITICAL_SECTION*>(out.manager + 0xd0);
                    locked = TryEnterCriticalSection(lock) != 0;
                    if (locked) {
                        const auto manager = out.manager;
                        const auto entries = *reinterpret_cast<std::uintptr_t*>(manager + 0x100);
                        out.slots = *reinterpret_cast<std::int32_t*>(manager + 0x108);
                        out.free = *reinterpret_cast<std::int32_t*>(manager + 0x134);
                        out.bits = *reinterpret_cast<std::int32_t*>(manager + 0x128);
                        out.capacity = *reinterpret_cast<std::int32_t*>(manager + 0x12c);
                        auto bitmap = *reinterpret_cast<std::uintptr_t*>(manager + 0x120);
                        if (!bitmap) bitmap = manager + 0x110;
                        valid = out.slots >= 0 && out.slots <= 128 && out.free >= 0
                            && out.free <= out.slots && out.bits == out.slots
                            && out.capacity >= out.bits && (out.slots == 0 || entries != 0);
                        if (valid) {
                            for (std::int32_t i = 0; i < out.bits; ++i) {
                                if (!(*reinterpret_cast<std::uint32_t*>(bitmap + (i / 32) * 4)
                                    & (std::uint32_t{1} << (i % 32)))) continue;
                                auto& row = out.entries[out.count++];
                                row.slot = static_cast<std::uint32_t>(i);
                                const auto address = entries + static_cast<std::uintptr_t>(i) * 0x70;
                                std::memcpy(row.bytes.data(), reinterpret_cast<void*>(address), row.bytes.size());
                                if (*reinterpret_cast<std::int32_t*>(address + 0x50) != 0) {
                                    row.binding = *reinterpret_cast<std::uintptr_t*>(address + 0x40);
                                    if (!row.binding) row.binding = address + 0x20;
                                    row.table = *reinterpret_cast<std::uintptr_t*>(row.binding);
                                    row.resolver = *reinterpret_cast<std::uintptr_t*>(row.table + 8);
                                }
                            }
                            valid = out.count == static_cast<std::size_t>(out.slots - out.free);
                        }
                    }
                }
            } __finally {
                if (locked) LeaveCriticalSection(lock);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
        return valid;
    }
    void ObserveCriCallbacks(std::uintptr_t base, unsigned tick) {
        CriCallbackSnapshot snapshot{};
        const bool valid = InspectCriCallbacks(base, snapshot);
        RC::Output::send<RC::LogLevel::Default>(STR(
            "[ReplayQualification] cri callback census native_tick={} valid={} manager={:x} slots={} free={} bits={} capacity={} occupied={} diagnostic_only=true\n"),
            tick, valid, snapshot.manager, snapshot.slots, snapshot.free, snapshot.bits, snapshot.capacity, snapshot.count);
        if (!valid) return;
        for (std::size_t i = 0; i < snapshot.count; ++i) {
            const auto& row = snapshot.entries[i];
            std::uintptr_t callback{};
            std::memcpy(&callback, row.bytes.data() + 0x10, sizeof(callback));
            std::wstring raw;
            raw.reserve(row.bytes.size() * 2);
            constexpr wchar_t hex[] = L"0123456789abcdef";
            for (auto byte : row.bytes) { raw.push_back(hex[byte >> 4]); raw.push_back(hex[byte & 15]); }
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayQualification] cri callback entry native_tick={} slot={} callback={:x} binding={:x} table={:x} resolver={:x} image_base={:x} raw={} diagnostic_only=true\n"),
                tick, row.slot, callback, row.binding, row.table, row.resolver, base, raw);
        }
    }
    static bool InspectObject(RC::Unreal::UObject* owner,ComponentFamily& out) noexcept {
        __try {
            if(!owner)return false;
            out.owner_index=owner->GetInternalIndex();
            const auto* item=RC::Unreal::FUObjectArray::IndexToObject(out.owner_index);
            if(!item || item->GetUObject()!=owner || !item->IsValid(false))return false;
            out.owner_serial=item->GetSerialNumber();
            auto* type=owner->GetClassPrivate();if(!type)return false;
            out.class_index=type->GetInternalIndex();
            const auto* class_item=RC::Unreal::FUObjectArray::IndexToObject(out.class_index);
            if(!class_item || class_item->GetUObject()!=type || !class_item->IsValid(false))return false;
            out.class_serial=class_item->GetSerialNumber();out.type=reinterpret_cast<std::uintptr_t>(type);
            out.object=reinterpret_cast<std::uintptr_t>(owner);
            out.table=*reinterpret_cast<std::uintptr_t*>(owner);
            out.destroy=*reinterpret_cast<std::uintptr_t*>(out.table);
            // Zero means no serial has been assigned yet. Report it honestly;
            // allocating a weak serial would mutate this read-only census.
            // Such a row identifies a candidate class, not a lifetime lease.
            return out.owner_serial>=0 && out.class_serial>=0;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool InspectComponent(std::uintptr_t tick,ComponentFamily& out) noexcept {
        __try {
            if(!InspectObject(*reinterpret_cast<RC::Unreal::UObject**>(tick+0x50),out))return false;
            out.consume=*reinterpret_cast<std::uintptr_t*>(out.table+0x300);
            out.query=*reinterpret_cast<std::uintptr_t*>(out.table+0x350);
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool InspectTraceRoots(std::uintptr_t component,std::array<ComponentFamily,2>& roots) noexcept {
        __try {
            auto* chara=*reinterpret_cast<RC::Unreal::UObject**>(component+0x490);
            if(!InspectObject(chara,roots[0]))return false;
            auto* scene=*reinterpret_cast<RC::Unreal::UObject**>(roots[0].object+0x168);
            return InspectObject(scene,roots[1])
                && *reinterpret_cast<std::uintptr_t*>(roots[1].object+0x190)==roots[0].object;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    std::array<ComponentFamily,128> component_families_{};
    std::size_t component_family_count_{};
    unsigned cri_census_tick_{};
    std::array<TaskFamily, 128> task_families_{};
    std::size_t task_family_count_{};
    std::mutex task_mutex_;
    std::atomic_bool task_failed_{};
    std::unique_ptr<PLH::x64Detour> callbacks_{}, round_{}, tail_{}, task_{}, input_{};
};
