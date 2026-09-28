#pragma once
#include <cstddef>
#include <cstdint>
#include <array>
#include <utility>

namespace Horse::Deterministic {
struct ReplayHudOwnerRoute {
    std::uintptr_t manager{},controller{},damage_listener{};
    bool valid{};
};
// Capture still rejects extra battle-owned instances, as global discovery did.
// Memoization lasts only for this synchronous read-only census. Collisions
// recompute the original class/superclass predicate; no result survives a call.
class ReplayHudOwnerCensus {
public:
    explicit ReplayHudOwnerCensus(const ReplayHudOwnerRoute& route):route_(route) {}
    template<class Classify> unsigned MatchClass(std::uintptr_t type,Classify&& classify) {
        const auto slot=2*(((type>>4)^(type>>16))%(classes_.size()/2));
        auto& entry=classes_[slot];auto& previous=classes_[slot+1];
        if(previous.present && previous.type==type)std::swap(entry,previous);
        if(!entry.present || entry.type!=type) {
            const auto match=classify(type);
            previous=entry;entry={type,match,true};
        }
        return entry.match;
    }
    bool Observe(std::uintptr_t object,unsigned match) noexcept {
        if(match&~3u)return valid_=false;
        if((match&1u) && (object!=route_.controller || ++controllers_!=1))valid_=false;
        if((match&2u) && (object!=route_.damage_listener || ++listeners_!=1))valid_=false;
        return valid_;
    }
    bool complete() const noexcept {return route_.valid && valid_ && controllers_==1 && listeners_==1;}
private:
    struct Class {std::uintptr_t type{};unsigned match{};bool present{};};
    std::array<Class,64> classes_{};
    ReplayHudOwnerRoute route_{};
    unsigned controllers_{},listeners_{};bool valid_{true};
};
// Native property registration 14094E9F4/14091BF30 and consumer
// 1403FB56B establish this battle-owned route. No global discovery or cache.
// The caller additionally checks UObject lifetimes and reflected field offsets.
template<class Read>
ReplayHudOwnerRoute InspectReplayHudOwnerRoute(Read&& read,std::uintptr_t base,std::uintptr_t battle) noexcept {
    ReplayHudOwnerRoute out{};
    const auto field=[&](std::uintptr_t object,std::size_t offset,std::uintptr_t& value) {
        return object && object<=UINTPTR_MAX-offset && read(object+offset,&value,sizeof(value));
    };
    const auto owner=[&](std::uintptr_t object,std::uintptr_t table) {
        std::uintptr_t actual{},parent{};
        return field(object,0,actual) && actual==base+table && field(object,0x98,parent) && parent==battle;
    };
    if(!base || base>UINTPTR_MAX-0x3281920 || !battle
        || !field(battle,0x530,out.manager) || !owner(out.manager,0x3281920)
        || !field(out.manager,0x3b8,out.controller) || !owner(out.controller,0x327e810)
        || !field(out.manager,0x3a0,out.damage_listener) || !owner(out.damage_listener,0x327b740))return out;
    out.valid=true;return out;
}
}
