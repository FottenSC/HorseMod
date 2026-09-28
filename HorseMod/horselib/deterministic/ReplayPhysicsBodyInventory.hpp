#pragma once
#include <array>
#include <cstdint>

namespace Horse::Deterministic {
// A read-only census from the existing locked scene scan. Native addresses are
// process-local witnesses, not logical rollback identities or lifetime leases.
struct ReplayPhysicsBodyInventory final {
    enum class Role : std::uint8_t { Static, UnrelatedDynamic, GroundChild };
    struct Body {
        std::uintptr_t scene{},actor{},body{},component{},type{};
        unsigned body_flags{},actor_flags{};
        Role role{Role::Static};
        // Direct FUObjectArray slot witness. Zero serial means no generation
        // was available; the raw component pointer alone remains provisional.
        int component_index{-1},component_serial{};
        unsigned scene_slot{}; // Position in the locked native actor table.
        friend bool operator==(const Body&,const Body&)=default;
    };
    static constexpr unsigned maximum_bodies=128; // Two admitted scenes, 64 actors each.
    std::array<Body,maximum_bodies> bodies{};
    unsigned count{};
    bool scan_complete{};
    bool Add(const Body& body) noexcept {
        if(scan_complete || !body.scene || !body.actor || !body.type || count==bodies.size())return false;
        for(unsigned i=0;i<count;++i)
            if(bodies[i].scene==body.scene && bodies[i].actor==body.actor)return false;
        bodies[count++]=body;return true;
    }
    enum class Difference : std::uint8_t { None, Incomplete, Removed, Changed, Added };
    static const char* Name(Difference difference) noexcept {
        switch(difference) {
        case Difference::None:return "none";
        case Difference::Incomplete:return "incomplete";
        case Difference::Removed:return "removed";
        case Difference::Changed:return "changed";
        case Difference::Added:return "added";
        }
        return "unknown";
    }
    struct Change {Difference kind{Difference::Incomplete};Body before{},after{};};
    Change FirstDifference(const ReplayPhysicsBodyInventory& after) const noexcept {
        if(!scan_complete || !after.scan_complete)return {};
        for(unsigned i=0;i<count;++i) {
            const auto& before=bodies[i];
            unsigned j{};
            for(;j<after.count;++j)
                if(after.bodies[j].scene==before.scene && after.bodies[j].actor==before.actor)break;
            if(j==after.count)return {Difference::Removed,before,{}};
            if(!(before==after.bodies[j]))return {Difference::Changed,before,after.bodies[j]};
        }
        for(unsigned j=0;j<after.count;++j) {
            unsigned i{};
            for(;i<count;++i)
                if(bodies[i].scene==after.bodies[j].scene && bodies[i].actor==after.bodies[j].actor)break;
            if(i==count)return {Difference::Added,{},after.bodies[j]};
        }
        return {Difference::None,{},{}};
    }
};
}
