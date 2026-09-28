#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
namespace Horse::Deterministic {
// Captured binding values, never ownership of a native MID or render resource.
// The native cold owner separately constructs and retires every replacement.
struct ReplayParticleMaterialGraph {
    struct Scalar {std::uint64_t name{};float value{};friend bool operator==(const Scalar&,const Scalar&)=default;};
    struct Vector {std::uint64_t name{};std::array<float,4> value{};friend bool operator==(const Vector&,const Vector&)=default;};
    struct Slot {
        std::uintptr_t object{},parent{};
        std::vector<Scalar> scalars;std::vector<Vector> vectors;
        bool ValuesEqual(const Slot& b) const {return bool(object)==bool(b.object) && parent==b.parent && scalars==b.scalars && vectors==b.vectors;}
    };
    struct Array {std::uintptr_t data{};int count{},capacity{};std::array<Slot,32> slots{};};
    std::array<Array,3> arrays{}; // MIDBaseList, MIDTranslucentList, EmitterMaterials
    unsigned materials{};float fade_duration{};bool ready{};
    const char* check{"none"};
    template<class T>static T Read(std::uintptr_t p,std::size_t offset=0) {
        T value{};std::memcpy(&value,reinterpret_cast<const void*>(p+offset),sizeof(value));return value;
    }
    static bool CaptureSlot(std::uintptr_t base,std::uintptr_t component,std::uintptr_t object,Slot& slot) {
        slot={};slot.object=object;if(!object)return true;
        if(Read<std::uintptr_t>(object)!=base+0x391ee70 || Read<std::uintptr_t>(object,0x20)!=component)return false;
        slot.parent=Read<std::uintptr_t>(object,0x78);if(!slot.parent)return false;
        // Lux-created MIDs start with empty font/texture overrides and renamed
        // texture cache. Their audited mutable consumers write scalar/vector
        // values with zero expression GUIDs (141F1E200/141F1E420).
        if(Read<int>(object,0x90) || Read<int>(object,0xb0) || Read<int>(object,0x1c0))return false;
        for(unsigned side=0;side<2;++side) {
            const auto offset=side?0xb8:0x98;const auto stride=side?40u:32u;
            const auto data=Read<std::uintptr_t>(object,offset);
            const auto count=Read<int>(object,offset+8),capacity=Read<int>(object,offset+12);
            if(count<0 || capacity<count || capacity>64 || (capacity&&!data))return false;
            for(int i=0;i<count;++i) {
                const auto row=data+std::size_t(i)*stride,name=Read<std::uint64_t>(row);
                if(!name || Read<std::uint64_t>(row,side?24:12) || Read<std::uint64_t>(row,side?32:20))return false;
                if(side) {
                    for(const auto& old:slot.vectors)if(old.name==name)return false;
                    slot.vectors.push_back({name,Read<std::array<float,4>>(row,8)});
                } else {
                    for(const auto& old:slot.scalars)if(old.name==name)return false;
                    slot.scalars.push_back({name,Read<float>(row,8)});
                }
            }
        }
        return true;
    }
    std::size_t owned_bytes() const {
        std::size_t bytes=sizeof(*this);
        for(const auto& row:arrays)for(const auto& slot:row.slots)
            bytes+=slot.scalars.capacity()*sizeof(Scalar)+slot.vectors.capacity()*sizeof(Vector);
        return bytes;
    }
    // Logical counterpart for a material reference; external authored assets
    // retain identity. Never use this to rewrite an arbitrary pointer field.
    std::uintptr_t Translate(std::uintptr_t object,const ReplayParticleMaterialGraph& fresh) const {
        if(!object)return 0;
        for(unsigned side=0;side<2;++side)for(int i=0;i<arrays[side].count;++i)
            if(arrays[side].slots[i].object==object)
                return i<fresh.arrays[side].count?fresh.arrays[side].slots[i].object:0;
        return object;
    }
    bool ValuesEqual(const ReplayParticleMaterialGraph& b) const {
        if(!ready || !b.ready || materials!=b.materials || fade_duration!=b.fade_duration)return false;
        for(unsigned side=0;side<3;++side) {
            if(arrays[side].count!=b.arrays[side].count)return false;
            for(int i=0;i<arrays[side].count;++i) {
                if(side<2) {if(!arrays[side].slots[i].ValuesEqual(b.arrays[side].slots[i]))return false;}
                else if(Translate(arrays[side].slots[i].object,b)!=b.arrays[side].slots[i].object)return false;
            }
        }
        return true;
    }
    template<class Native>
    bool Realize(std::uintptr_t base,std::uintptr_t component,Native& native) const {
        if(!ready)return false;
        for(unsigned side=0;side<3;++side) {
            const auto offset=side<2?0xa98+side*16:0x810;
            const auto count=Read<int>(component,offset+8);
            if(count) {
                if(side==2 || count!=arrays[side].count)return false;
                const auto data=Read<std::uintptr_t>(component,offset);if(!data)return false;
                for(int i=0;i<count;++i)if(arrays[side].slots[i].object || Read<std::uintptr_t>(data,i*8))return false;
            }
        }
        for(unsigned side=0;side<2;++side)if(arrays[side].count && !Read<int>(component,0xaa0+side*16)) {
            std::array<std::uintptr_t,32> parents{};
            for(int i=0;i<arrays[side].count;++i)parents[i]=arrays[side].slots[i].parent;
            if(side && !(fade_duration>0))return false;
            if(!native.Initialize(side,parents.data(),arrays[side].count,fade_duration))return false;
            const auto offset=0xa98+side*16;
            const auto data=Read<std::uintptr_t>(component,offset);const auto count=Read<int>(component,offset+8);
            if(count!=arrays[side].count || (count&&!data))return false;
            for(int i=0;i<count;++i) {
                const auto& old=arrays[side].slots[i];const auto object=Read<std::uintptr_t>(data,i*8);
                if(!old.object) {if(object)return false;continue;}
                Slot fresh;
                if(!CaptureSlot(base,component,object,fresh) || !fresh.object || fresh.parent!=old.parent
                    || !fresh.scalars.empty() || !fresh.vectors.empty())return false;
                for(unsigned prior=0;prior<2;++prior)for(int j=0;j<arrays[prior].count;++j)
                    if(object==arrays[prior].slots[j].object)return false;
                for(const auto& value:old.scalars)if(!native.Scalar(object,value.name,value.value))return false;
                for(const auto& value:old.vectors)if(!native.Vector(object,value.name,value.value.data()))return false;
            }
        }
        ReplayParticleMaterialGraph fresh;
        if(!fresh.Capture(base,component))return false;
        for(int i=0;i<arrays[2].count;++i) {
            const auto object=Translate(arrays[2].slots[i].object,fresh);
            if(arrays[2].slots[i].object && !object)return false;
            if(!native.Override(i,object))return false;
        }
        if(!fresh.Capture(base,component))return false;
        // Clock state itself belongs to the component projection. Initialization
        // only needs the captured duration to construct the translucent roots.
        fresh.fade_duration=fade_duration;
        return ValuesEqual(fresh);
    }
    bool Capture(std::uintptr_t base,std::uintptr_t component) {
        *this={};fade_duration=Read<float>(component,0xabc);
        for(unsigned side=0;side<3;++side) {
            const auto offset=side<2?0xa98+side*16:0x810;
            auto& row=arrays[side];row.data=Read<std::uintptr_t>(component,offset);
            row.count=Read<int>(component,offset+8);row.capacity=Read<int>(component,offset+12);
            if(row.count<0 || row.capacity<row.count || row.capacity>32 || (row.capacity&&!row.data)) {
                check="source_material_capacity";return false;
            }
            for(int i=0;i<row.count;++i) {
                auto& slot=row.slots[i];slot.object=Read<std::uintptr_t>(row.data,i*8);
                if(side<2 && slot.object) {
                    if(!CaptureSlot(base,component,slot.object,slot)) {check="source_material_values";return false;}
                    for(unsigned earlier=0;earlier<=side;++earlier)
                        for(int j=0;j<(earlier==side?i:arrays[earlier].count);++j)
                            if(arrays[earlier].slots[j].object==slot.object) {check="source_material_alias";return false;}
                    ++materials;
                }
            }
        }
        ready=true;return true;
    }
};
}
