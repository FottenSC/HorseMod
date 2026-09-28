#pragma once
#include <array>
#include <cstdint>

namespace Horse::Deterministic {
// A native creation component can outlive its scene proxy. Visibility is
// captured state; component generation, actor-array membership and every
// other flag remain binding checks. Never use a generated name as identity.
struct ReplayCreationRenderOwner {
    enum class Membership : std::uint8_t { CreationArray, WeaponSlot } membership{};
    bool weapon() const noexcept { return membership==Membership::WeaponSlot; }
    bool ValidMembership(std::uintptr_t live_storage, std::int32_t live_count,
        std::int32_t live_capacity, std::uintptr_t live_component) const noexcept {
        if(membership!=Membership::CreationArray && membership!=Membership::WeaponSlot) return false;
        if(weapon() && (storage!=owner+0x390 || slot!=0 || count!=1 || capacity!=1)) return false;
        return component && owner && storage && slot>=0 && slot<count && count<=capacity
            && live_storage==storage && live_count==count && live_capacity==capacity && live_component==component;
    }
    template<class Read> bool LiveMembership(Read read) const {
        if(!ValidMembership(storage,count,capacity,component))return false;
        std::uintptr_t backing=owner+0x390,live_component{};
        std::int32_t live_count=1,live_capacity=1;
        if(!weapon() && (!read(owner+0x3b0,backing) || !read(owner+0x3b8,live_count) || !read(owner+0x3bc,live_capacity)))return false;
        // Reject changed array ownership before dereferencing its backing.
        return backing==storage && live_count==count && live_capacity==capacity
            && read(backing+std::uintptr_t(slot)*sizeof(std::uintptr_t),live_component)
            && ValidMembership(backing,live_count,live_capacity,live_component);
    }
    std::uintptr_t component{}, owner{}, storage{}, parent{}, mesh{}, animation{};
    std::array<std::int32_t, 2> weak{}, owner_weak{};
    std::int32_t slot{}, count{}, capacity{};
    std::uint32_t flags{}, visibility{}, primitive_id{};
    friend bool operator==(const ReplayCreationRenderOwner&, const ReplayCreationRenderOwner&) = default;
    bool SameBinding(const ReplayCreationRenderOwner& other) const noexcept {
        auto a=*this,b=other;
        a.visibility&=~0x10u;b.visibility&=~0x10u;
        return a==b;
    }
};
}
