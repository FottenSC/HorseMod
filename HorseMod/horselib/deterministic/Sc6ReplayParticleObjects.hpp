#pragma once
#include "Sc6ReplayVfxState.hpp"
#include "Sc6ReplayWorldState.hpp"
#include "Sc6ReplayObjectVisit.hpp"
#include <Unreal/UObject.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>

namespace Horse::Deterministic
{
inline Status CaptureReplayWorld(Sc6ReplayWorldState& output,std::uintptr_t base,void* world,std::size_t budget) noexcept
{
    std::array<std::uintptr_t,128> actors{};std::size_t count{};
    if(budget<sizeof(actors))return Status::failure(FailureCode::CapacityExceeded);
    auto status=Status::success();
    try {
        using namespace RC::Unreal;
        UObjectGlobals::ForEachUObject([&](UObject* object,std::int32_t,std::int32_t){
            if(!object)return RC::LoopAction::Continue;
            // The inventory already supplies an allocated UObject header.
            // Exclude unrelated vtables before expensive lifetime resolution;
            // every stage candidate still passes the original owner checks.
            ReplayStageVisibility::Kind kind{};
            if(!ReplayStageVisibility::Classify(base,*reinterpret_cast<const std::uintptr_t*>(object),kind))
                return RC::LoopAction::Continue;
            const auto* item=FUObjectArray::IndexToObject(object->GetInternalIndex());
            if(!item || item->GetUObject()!=object || !item->IsValid(false)
                || object->HasAnyFlags(static_cast<EObjectFlags>(RF_ClassDefaultObject|RF_ArchetypeObject)))return RC::LoopAction::Continue;
            if(object->GetWorld()!=world)return RC::LoopAction::Continue;
            if(count==actors.size()){status=Status::failure(FailureCode::CapacityExceeded);return RC::LoopAction::Break;}
            actors[count++]=reinterpret_cast<std::uintptr_t>(object);return RC::LoopAction::Continue;
        });
        if(!status.ok())return status;
        const auto captured=output.Capture(base,world,budget-sizeof(actors),{actors.data(),count});
        if(!captured.ok()) {
            const ReplayStageVisibility* last{};
            for(const auto& row:output.stage_visibility()) {if(!row.actor)break;last=&row;}
            if(last)RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] stage source capture rejected actor={} kind={} requested={} break={} alpha={} rate={} members={} source_values_supported={}\n"),
                reinterpret_cast<UObject*>(last->actor)->GetFullName(),static_cast<unsigned>(last->kind),last->values.requested,last->values.break_state,
                last->values.alpha,last->values.rate,last->component_count,last->SupportedValues());
        }
        return captured;
    } catch(...) {return Status::failure(FailureCode::ContextUnavailable);}
}
// UE-facing enumeration stays out of the reusable native checkpoint library.
// One completed-application scan, bounded before storage growth. This is not
// a per-tick observer and does not grant component lifetime ownership.
inline Status CaptureReplayVfx(Sc6ReplayVfxState& output, std::uintptr_t base,
    void* battle, std::size_t budget, std::span<void* const> companion_owners = {},
    const Sc6ReplayVfxState::ParticleBirthSet* quarantined = nullptr) noexcept
{
    std::array<void*, 4096> candidates{};
    if (budget <= sizeof(candidates) + 8192) return Status::failure(FailureCode::CapacityExceeded);
    std::size_t count{};
    auto status = Status::success();
    std::size_t quarantined_seen{};
    if(quarantined) for(const auto& birth:quarantined->members()) {
        const char* check{};
        status=birth.ValidateQuarantined(&check);
        if(!status.ok()) {
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] particle quarantine capture rejection component={:x} check={} code={} before_inventory=true\n"),
                birth.component(),RC::to_generic_string(check?check:"unknown"),static_cast<unsigned>(status.code));
            return status;
        }
    }
    try
    {
        using namespace RC::Unreal;
        auto* replay_world=reinterpret_cast<UObject*>(battle)->GetWorld();
        if(!replay_world) return Status::failure(FailureCode::ContextUnavailable);
        // Reuse the full-inventory class visitor and its call-local ancestry
        // memoization. Its bounded scratch fits the existing 8192-byte reserve.
        // Only class/default-object exclusion moves before lifetime resolution.
        const bool complete=VisitReplayObjectsOfClass(L"ParticleSystemComponent",[&](UObject* object) {
            const auto* item = FUObjectArray::IndexToObject(object->GetInternalIndex());
            if (!item || item->GetUObject() != object || !item->IsValid(false)
                || object->HasAnyFlags(static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_ArchetypeObject)))
                return true;
            if(object->GetWorld()!=replay_world) return true;
            // Only live registered components belong to this world's boundary.
            if(!(*reinterpret_cast<const unsigned*>(reinterpret_cast<const std::byte*>(object)+0x188)&1))
                return true;
            if(quarantined) for(const auto& birth:quarantined->members())
                if(birth.component()==reinterpret_cast<std::uintptr_t>(object)) {++quarantined_seen;return true;}
            if (count == candidates.size())
            {
                status = Status::failure(FailureCode::CapacityExceeded);
                return false;
            }
            candidates[count++] = object;
            return true;
        });
        if(!complete && status.ok())return Status::failure(FailureCode::CaptureFailed);
        if (!status.ok()) return status;
        if(quarantined && quarantined_seen!=quarantined->members().size())
            return Status::failure(FailureCode::GenerationMismatch);
        // Excluded owners are independently checked again after enumeration.
        // Slots still naming one will capture it and fail the pool partition;
        // exclusion never silently edits the native manager or active inputs.
        if(quarantined) for(const auto& birth:quarantined->members()) {
            status=birth.ValidateQuarantined();if(!status.ok()) return status;
        }
        const auto captured=output.Capture(base, battle, budget - sizeof(candidates) - 8192, {candidates.data(), count}, companion_owners);
        if(captured.code==FailureCode::UnsupportedContent)
        {
            const auto rejected=output.capture_rejection();
            auto* component=reinterpret_cast<UObject*>(rejected.component);
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] particle capture rejected code={} component={} emitter={:x}\n"),
                static_cast<unsigned>(captured.code), component ? component->GetFullName() : STR("none"), rejected.emitter);
            if(component)
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] particle capture contract tick={:x} flags908={}\n"),
                    (*reinterpret_cast<std::uintptr_t**>(component))[0x300/8]-base,
                    *reinterpret_cast<const unsigned char*>(rejected.component+0x908));
            if(rejected.emitter)
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] emitter capture contract vtable={:x} module={:x} module_bytes={} attached={:x} attached_count={} attached_capacity={}\n"),
                    *reinterpret_cast<const std::uintptr_t*>(rejected.emitter)-base,
                    *reinterpret_cast<const std::uintptr_t*>(rejected.emitter+0x100),
                    *reinterpret_cast<const int*>(rejected.emitter+0x108),
                    *reinterpret_cast<const std::uintptr_t*>(rejected.emitter+0x1c0),
                    *reinterpret_cast<const int*>(rejected.emitter+0x1c8),
                    *reinterpret_cast<const int*>(rejected.emitter+0x1cc));
            if(rejected.emitter)
            {
                const auto authored=*reinterpret_cast<const std::uintptr_t*>(rejected.emitter+0x10);
                const auto modules=*reinterpret_cast<UObject* const* const*>(authored+0x158);
                const auto module_count=*reinterpret_cast<const int*>(authored+0x160);
                if(module_count>=0 && module_count<=64 && (!module_count || modules))
                    for(int i=0;i<module_count;++i)
                    {
                        auto* module=modules[i];
                        const auto* item=module ? FUObjectArray::IndexToObject(module->GetInternalIndex()) : nullptr;
                        if(!item || item->GetUObject()!=module || !item->IsValid(false)) break;
                        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] emitter payload module ordinal={} name={} initializer={:x}\n"),
                            i,module->GetFullName(),(*reinterpret_cast<const std::uintptr_t* const*>(module))[0x270/8]-base);
                    }
            }
        }
        return captured;
    }
    catch (...) { return Status::failure(FailureCode::ContextUnavailable); }
}
}
