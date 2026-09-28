#pragma once
#include "ReplayGroundBodyAdmission.hpp"
#include "ReplayGroundPrivateBody.hpp"
#include "ReplayGroundShapeState.hpp"
#include <array>
#include <cstring>
#include <utility>

namespace Horse::Deterministic {
// Owned semantic values read at capture, never an expected observation or a
// native actor image. This narrow route covers initial sleeping kinematics.
// Scene/solver/query identities and shape bindings remain separate owners.
struct ReplayGroundInitialBodyState {
    std::array<std::byte,0xe0> values{};
    // Shipped getters3C220/3C330 consume BodyToActor+B0 and buffered
    // BodyToWorld+140; simulation consumes BodyCore+90. Preserve the coupled
    // source poses, not a lossy global-pose/COM setter round trip.
    std::array<std::array<std::byte,28>,3> poses{};
    ReplayGroundShapeState shapes;
    bool ready{};
    static constexpr std::array<std::pair<unsigned,unsigned>,13> fields{{
        {0x10,1},{0x28,28},{0x48,28},{0x64,32},{0x84,24},{0x9c,1},
        {0xa0,12},{0xb0,12},{0xbc,1},{0xc0,8},{0xc8,2},{0xcc,12},{0xd8,4}}};
    template<class T> T Value(unsigned offset)const noexcept {
        T value{};std::memcpy(&value,values.data()+offset,sizeof(value));return value;
    }
    template<class Read,class Values> bool Capture(Read read,Values get_values,std::uintptr_t actor,
        ReplayGroundShapeState::Binding binding) noexcept {
        if(ready || !actor)return false;
        std::array<std::byte,0xf0> native{};
        if(!get_values(actor,native))return false;
        unsigned char flags=std::to_integer<unsigned char>(native[0x9c]);
        if(flags!=3 || native[0xbc]!=std::byte{1}
            || !ReplayGroundBodyAdmission::Flags(read,actor,flags,std::to_integer<unsigned char>(native[0x10])))return false;
        // No target, velocity, wake countdown or buffered work may be silently
        // dropped by this initial-state route.
        for(unsigned offset=0x84;offset<0x9c;offset+=4) {
            unsigned value{};std::memcpy(&value,native.data()+offset,4);if(value&0x7fffffffu)return false;
        }
        unsigned wake{};std::memcpy(&wake,native.data()+0xcc,4);if(wake&0x7fffffffu)return false;
        for(unsigned i=0;i<poses.size();++i) {
            if(!read(actor+std::array<unsigned,3>{0x90,0xb0,0x140}[i],poses[i]))return false;
            for(unsigned lane=0;lane<7;++lane) {
                unsigned value{};std::memcpy(&value,poses[i].data()+lane*4,4);
                if((value&0x7f800000u)==0x7f800000u)return false;
            }
        }
        for(const auto [offset,size]:fields)std::memcpy(values.data()+offset,native.data()+offset,size);
        std::uintptr_t table{};
        if(!read(actor,table) || table<0x19b7c0 || !shapes.Capture(read,actor,table-0x19b7c0,binding))return false;
        ready=true;return true;
    }
    template<class Values> bool Matches(Values get_values,std::uintptr_t actor,unsigned* mismatch=nullptr)const noexcept {
        std::array<std::byte,0xf0> native{};
        if(!ready || !get_values(actor,native))return false;
        for(const auto [offset,size]:fields)for(unsigned i=0;i<size;++i)if(native[offset+i]!=values[offset+i]) {
            if(mismatch)*mismatch=offset+i;return false;
        }
        return true;
    }
    // Native scalar setters operate on private Scb/BodyCore state. Exact
    // getter comparison includes inverse mass/inertia, detecting lossy setter
    // round trips rather than accepting completion as proof of restoration.
    template<class Read,class Write,class Scene,class Values> bool Install(Read read,Write write,Scene scene,Values get_values,
        const ReplayGroundPrivateBody& owner,unsigned* mismatch=nullptr,
        ReplayGroundShapeState::Binding binding={})const noexcept {
        if(mismatch)*mismatch=0xffff0001u;
        if(!ready || !owner.Validate(read,scene) || !shapes.Install(read,write,scene,owner,binding,mismatch))return false;
        const auto actor=owner.actor,module=owner.module;
        constexpr std::array<std::pair<unsigned,unsigned>,16> slots{{
            {0x58,0x3cf70},{0xf8,0x3d7c0},
            {0x110,0x3d7e0},{0x170,0x3ded0},{0x180,0x3d990},{0x190,0x3d980},
            {0x1a0,0x3d970},{0x1c0,0x3aac0},{0x1d0,0x3ab00},{0x1e0,0x3ad00},
            {0x1f8,0x3b0b0},{0x208,0x3b0f0},{0x228,0x3b250},{0x248,0x3b140},
            {0x250,0x3b150},{0x268,0x3b1c0}}};
        std::uintptr_t table{};if(!read(actor,table))return false;
        for(const auto [slot,rva]:slots){std::uintptr_t entry{};
            if(!read(table+slot,entry) || entry!=module+rva){if(mismatch)*mismatch=0xff000000u|slot;return false;}}
        const auto flags=Value<unsigned char>(0x9c),actor_flags=Value<unsigned char>(0x10);
        reinterpret_cast<void(*)(std::uintptr_t,const void*)>(module+0x3ded0)(actor,&flags);
        reinterpret_cast<void(*)(std::uintptr_t,const void*)>(module+0x3cf70)(actor,&actor_flags);
        reinterpret_cast<void(*)(std::uintptr_t,float)>(module+0x3d7c0)(actor,Value<float>(0x64));
        reinterpret_cast<void(*)(std::uintptr_t,const void*)>(module+0x3d7e0)(actor,values.data()+0x6c);
        for(const auto [rva,offset]:{std::pair{0x3d990u,0xa0u},{0x3d980u,0xa4u},{0x3d970u,0xa8u},
            {0x3aac0u,0xb0u},{0x3ab00u,0xb4u},{0x3ad00u,0xb8u},{0x3b0b0u,0xc0u},
            {0x3b0f0u,0xc4u},{0x3b1c0u,0xd8u}})
            reinterpret_cast<void(*)(std::uintptr_t,float)>(module+rva)(actor,Value<float>(offset));
        const auto locks=Value<unsigned short>(0xc8);
        reinterpret_cast<void(*)(std::uintptr_t,const void*)>(module+0x3b250)(actor,&locks);
        reinterpret_cast<void(*)(std::uintptr_t,unsigned,unsigned)>(module+0x3b150)(actor,Value<unsigned>(0xd0),Value<unsigned>(0xd4));
        reinterpret_cast<void(*)(std::uintptr_t)>(module+0x3b140)(actor);
        // No BodySim, scene query handle, buffered write or scene consumer may
        // observe intermediate poses. Scene insertion later creates its caches
        // from these coherent native values; it is not part of this operation.
        unsigned buffered{};
        if(mismatch)*mismatch=0xffff0002u;
        if(!owner.Validate(read,scene) || !read(actor+0x17c,buffered) || buffered)return false;
        for(unsigned i=0;i<poses.size();++i)
            if(!write(actor+std::array<unsigned,3>{0x90,0xb0,0x140}[i],poses[i]))return false;
        if(mismatch)*mismatch=0xffff0003u;
        if(!owner.Validate(read,scene))return false;
        if(!Matches(get_values,actor,mismatch))return false;
        for(unsigned i=0;i<poses.size();++i) {
            std::array<std::byte,28> actual{};const auto offset=std::array<unsigned,3>{0x90,0xb0,0x140}[i];
            if(!read(actor+offset,actual) || actual!=poses[i]) {
                if(mismatch)*mismatch=0xfe000000u|offset;return false;
            }
        }
        if(mismatch)*mismatch=0xffff0004u;
        // BodyCore E0180 (reached by39ED0) reports no target for null
        // SimStateData. The live-body removal guard intentionally requires
        // that allocation; private construction must not invent one.
        std::uintptr_t state{};unsigned char tag{},target{};
        if(!read(actor+0x130,state) || (state &&
            (!read(state+0x1f,tag) || tag!=1 || !read(state+0x1c,target) || target)))return false;
        if(mismatch)*mismatch=0;
        return true;
    }
};
}

