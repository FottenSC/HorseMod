#pragma once
#include <array>
#include <cstdint>
#include <utility>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>

namespace Horse::Deterministic {
struct ReplayObjectVisitClassEntry {
    std::uintptr_t type{};
    bool matched{}, present{};
};
inline constexpr std::size_t ReplayObjectVisitScratchBytes=64*sizeof(ReplayObjectVisitClassEntry);
// Same class/superclass and default-object exclusions as this framework's
// FindAllOf, without collecting an unbounded intermediate object list.
// A visitor returning false stops discovery and reports admission failure.
template<class Visitor>
bool VisitReplayObjectsOfClass(const wchar_t* name, Visitor&& visitor)
{
    using namespace RC::Unreal;
    const FName class_name(name);
    // Class ancestry is immutable during this synchronous inventory visit.
    // Exact keys are checked on collisions; nothing is retained across calls.
    std::array<ReplayObjectVisitClassEntry,64> classes{};
    bool complete = true;
    UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
        if (!object) return RC::LoopAction::Continue;
        UStruct* type = object->GetClassPrivate();
        if (!type || object->HasAnyFlags(static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_ArchetypeObject)))
            return RC::LoopAction::Continue;
        const auto key=reinterpret_cast<std::uintptr_t>(type);
        const auto slot=2*(((key>>4)^(key>>16))%(classes.size()/2));
        auto& cached=classes[slot];
        auto& previous=classes[slot+1];
        // Two exact keys per bucket prevent alternating classes from evicting
        // each other. Same64 entries and per-visit lifetime; no object cache.
        if(previous.present && previous.type==key)std::swap(cached,previous);
        if(!cached.present || cached.type!=key) {
            bool matched=false;
            for(auto* ancestor=type;ancestor;ancestor=ancestor->GetSuperStruct())
                if(ancestor->GetNamePrivate().Equals(class_name)) {matched=true;break;}
            previous=cached;cached={key,matched,true};
        }
        if(cached.matched) {
            // IsA also walks the superclass chain. Only matching objects need
            // that exclusion; all unmatched objects were excluded either way.
            // Object identity, flags and lifetime checks are never cached.
            if (object->IsA<UClass>()) return RC::LoopAction::Continue;
            if (!visitor(object)) { complete = false; return RC::LoopAction::Break; }
        }
        return RC::LoopAction::Continue;
    });
    return complete;
}
}
