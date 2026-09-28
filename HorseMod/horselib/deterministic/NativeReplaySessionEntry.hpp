#pragma once
#include "DeterministicHookSet.hpp"
#include "Sc6ReplayNativeBridge.hpp"
#include "ReplayExecutableIdentity.hpp"
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <vector>

namespace Horse::Deterministic {
// Entry only. Once bound, Sc6ReplayHost owns indexing, execution and exit.
// No native profile, authored input, setup image or replay array is installed
// here: those must already exist through the game's normal replay load path.
class NativeReplaySessionEntry final {
    enum class Phase {AwaitingReplay,Active,Rejected};
    Phase phase_{};
    std::int32_t attempted_index_{-1},attempted_serial_{};
public:
    void Poll(std::uintptr_t base) noexcept
    {
        using namespace RC::Unreal;
        try {
            std::array<std::uint64_t,6> execution{};
            if(!DeterministicHookSet::ReadReplayExecutorStatus(execution.data(),execution.size())) return;
            if(execution[0]) return; // Includes holds and deferred exit cleanup.
            if(phase_==Phase::Active) phase_=Phase::AwaitingReplay;
            auto* replay_class=UObjectGlobals::StaticFindObject<UClass*>(nullptr,nullptr,
                STR("/Game/UI/GameFlow/GameScenes/Battle/ReplayBattleScene.ReplayBattleScene_C"));
            if(!replay_class) return;
            std::vector<UObject*> managers;UObjectGlobals::FindAllOf(STR("LuxUIGameFlowManager"),managers);
            UObject* scene{};
            for(auto* owner:managers) {
                if(!owner || !UObject::IsReal(owner)) continue;
                auto** current=owner->GetValuePtrByPropertyNameInChain<UObject*>(STR("CurrentScene"));
                if(!current || !*current || !UObject::IsReal(*current) || (*current)->GetClassPrivate()!=replay_class) continue;
                if(scene) return; // Ambiguous native scene ownership is never admitted.
                scene=*current;
            }
            if(!scene || !VerifiedReplayExecutable()) return;
            // 1403EF7A0 is the same nullable world-registry resolver called by
            // ULuxBattleFunctionLibrary::GetBattleManager's native thunk.
            auto* manager=reinterpret_cast<UObject*(*)(UObject*)>(base+0x3ef7a0)(scene);
            if(!manager || !UObject::IsReal(manager)) return;
            auto** player=manager->GetValuePtrByPropertyNameInChain<UObject*>(STR("BattleReplayPlayer"));
            if(!player || !*player || !UObject::IsReal(*player)) return;
            Sc6ReplayResolvers resolvers{};resolvers.image_base=base;resolvers.user=*player;
            resolvers.replay_player=[](void* value) noexcept -> void* {return value;};
            ReplaySourceState source{};
            if(!Sc6ReplayNativeBridge(resolvers).CapturePlaybackSource(source,true).ok() || !source.owner) return;
            const auto frame=*reinterpret_cast<const std::uint32_t*>(base+0x470d0c4);
            if(frame && !source.tracker_active) return; // Native setup can still reset the clock.
            std::array<int,2> weak{};
            reinterpret_cast<void(*)(void*,const void*)>(base+0xf7bad0)(weak.data(),manager);
            const auto* item=FUObjectArray::IndexToObject(weak[0]);
            if(!weak[1] || !item || item->GetUObject()!=manager || !item->IsValid(false) || item->GetSerialNumber()!=weak[1]) return;
            if(weak[0]==attempted_index_ && weak[1]==attempted_serial_) return;
            attempted_index_=weak[0];attempted_serial_=weak[1];
            if(frame || source.tracker_active || source.cursor || source.round>0) {
                phase_=Phase::Rejected;
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] automatic replay entry rejected tick={} reason=initial_baseline_missed native_reload_required=true\n"),frame);
                return;
            }
            if(!DeterministicHookSet::SetReplayExecutorEnabled(true,true,manager)) {
                phase_=Phase::Rejected;
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] automatic replay entry rejected tick=0 reason=executor_admission native_reload_required=true\n"));
                return;
            }
            ReplayTickIndex::Witness index{};
            if(!Sc6ReplayHost::IndexOperation(Sc6ReplayHost::IndexAction::BeginManaged,&index,4*1024*1024)
                || index.phase!=ReplayTickIndex::Phase::Recording || index.entries!=1) {
                phase_=Phase::Rejected;
                const bool stopping=DeterministicHookSet::SetReplayExecutorEnabled(false,false,nullptr);
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] automatic replay entry rejected tick=0 reason=index_admission stop_accepted={} native_reload_required=true\n"),stopping);
                return;
            }
            phase_=Phase::Active;
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] automatic replay session indexed native_tick=0 entries=1 source_round={} source_cursor=0 executor_owner=host checkpoint_placement=host\n"),source.round);
        } catch(...) {
            if(phase_!=Phase::Rejected)
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] automatic replay entry rejected reason=unavailable_native_context native_reload_required=true\n"));
            phase_=Phase::Rejected;
        }
    }
};
}
