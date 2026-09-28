#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace Horse::Deterministic {
// Process-local binding plus the values published by 1421B1900/141FB0F50.
// The caller owns the component, asset GC lease and ordered render/GPU boundary.
// No resource pointer, reference count, registration or texture is installed.
struct ReplayStaticVectorField {
    std::uintptr_t render{}, resource{}, texture{};
    std::array<std::byte, 0x28> resource_inputs{}; // dimensions, strength, bounds
    std::array<std::byte, 0x40> simulation_parameters{}; // native packet D0..10F -> render60..9F
    std::array<std::byte, 0xc0> transforms{}; // native transform publication
    std::array<std::byte, 8> parameters{}; // tightness/intensity
    std::uint8_t flags{}, enabled{};
    std::int32_t simulation_pass{};

    bool Capture(std::uintptr_t base, std::uintptr_t owner) noexcept {
        __try {
            if(!owner || Read<std::uintptr_t>(owner)!=base+0x394bfc0
                || Read<std::uint8_t>(owner+0x1e0) || Read<int>(owner+0xc8) || Read<int>(owner+0xd8)) return false;
            const auto field=Read<std::uintptr_t>(owner+0xe0);
            if(field && (Read<std::uintptr_t>(field)!=base+0x39e5a90
                || !Read<std::uintptr_t>(field+0x30))) return false;
            if(field)for(auto offset:{0x38,0x3c,0x40})
                if(Read<int>(field+offset)<=0 || Read<int>(field+offset)>2048) return false;
            render=owner;resource=field;texture=field?Read<std::uintptr_t>(field+0x30):0;
            resource_inputs={};
            if(field)std::memcpy(resource_inputs.data(),reinterpret_cast<void*>(field+0x38),resource_inputs.size());
            std::memcpy(simulation_parameters.data(),reinterpret_cast<void*>(owner+0x60),simulation_parameters.size());
            enabled=Read<std::uint8_t>(owner+0x253);simulation_pass=Read<int>(owner+0x244);
            std::memcpy(transforms.data(),reinterpret_cast<void*>(owner+0x110),transforms.size());
            std::memcpy(parameters.data(),reinterpret_cast<void*>(owner+0x1d0),parameters.size());
            flags=Read<std::uint8_t>(owner+0x1dc);
            return true;
        } __except(1) {return false;}
    }
    bool Bound(std::uintptr_t base) const noexcept {
        __try {
            return render && Read<std::uintptr_t>(render)==base+0x394bfc0
                && Read<std::uintptr_t>(render+0xe0)==resource
                && !Read<std::uint8_t>(render+0x1e0) && !Read<int>(render+0xc8) && !Read<int>(render+0xd8)
                && Read<std::uint8_t>(render+0x253)==enabled && Read<int>(render+0x244)==simulation_pass
                && (!resource || (Read<std::uintptr_t>(resource)==base+0x39e5a90
                && Read<std::uintptr_t>(resource+0x30)==texture
                && !std::memcmp(resource_inputs.data(),reinterpret_cast<void*>(resource+0x38),resource_inputs.size())))
                && (Read<std::uint8_t>(render+0x1dc)&0xf0)==(flags&0xf0);
        } __except(1) {return false;}
    }
    bool ResourceBound(std::uintptr_t base,std::uintptr_t asset) const noexcept {
        __try {
            if(!resource)return !asset && !texture;
            return resource && asset && Read<std::uintptr_t>(asset)==base+0x39e5068
                && Read<std::uintptr_t>(asset+0x58)==resource
                && Read<std::uintptr_t>(resource)==base+0x39e5a90
                && Read<std::uintptr_t>(resource+0x30)==texture
                && !std::memcmp(resource_inputs.data(),reinterpret_cast<void*>(resource+0x38),resource_inputs.size());
        } __except(1) {return false;}
    }
    bool FreshDestinationBound(std::uintptr_t base,std::uintptr_t asset) const noexcept {
        if(!ResourceBound(base,asset))return false;
        if(Bound(base))return true;
        __try {
            return render && Read<std::uintptr_t>(render)==base+0x394bfc0
                && !Read<std::uintptr_t>(render+0xe0) && !Read<std::uint8_t>(render+0x1e0)
                && !Read<int>(render+0xc8) && !Read<int>(render+0xd8) && Read<int>(render+0x240)==-1
                && !Read<std::uint8_t>(render+0x28) && !Read<std::uint8_t>(render+0x218)
                && Read<std::uint8_t>(render+0x253)==enabled && Read<int>(render+0x244)==simulation_pass
                && (Read<std::uint8_t>(render+0x1dc)&0xf0)==(flags&0xf0);
        } __except(1) {return false;}
    }
    bool InitializeFresh(std::uintptr_t base,std::uintptr_t asset) const noexcept {
        if(!FreshDestinationBound(base,asset))return false;
        if(Bound(base))return true;
        __try {
            // UVectorFieldStatic virtual+230: borrow asset+58 into instance+0,
            // clear instance+100 ownership. No allocation, registration or RNG.
            constexpr unsigned char code[]{0x48,0x8b,0x41,0x58,0x48,0x89,0x02,0xc6,0x82,0,1,0,0,0,0xc3};
            if(Read<std::uintptr_t>(base+0x39e5068+0x230)!=base+0x21a31f0
                || std::memcmp(reinterpret_cast<void*>(base+0x21a31f0),code,sizeof(code)))return false;
            reinterpret_cast<void(*)(std::uintptr_t,std::uintptr_t)>(base+0x21a31f0)(asset,render+0xe0);
            return Bound(base);
        } __except(1) {return false;}
    }
    bool AssetBound(std::uintptr_t base,std::uintptr_t asset) const noexcept {
        __try {
            if(!resource)return !asset && Bound(base);
            return Bound(base) && asset && Read<std::uintptr_t>(asset)==base+0x39e5068
                && Read<std::uintptr_t>(asset+0x58)==resource;
        } __except(1) {return false;}
    }
    bool Matches(std::uintptr_t base) const noexcept {
        __try {
            return Bound(base) && !std::memcmp(simulation_parameters.data(),reinterpret_cast<void*>(render+0x60),simulation_parameters.size())
                && !std::memcmp(transforms.data(),reinterpret_cast<void*>(render+0x110),transforms.size())
                && !std::memcmp(parameters.data(),reinterpret_cast<void*>(render+0x1d0),parameters.size())
                && Read<std::uint8_t>(render+0x1dc)==flags;
        } __except(1) {return false;}
    }
    bool Publish(std::uintptr_t base) const noexcept {
        if(!Bound(base))return false;
        __try {
            std::memcpy(reinterpret_cast<void*>(render+0x60),simulation_parameters.data(),simulation_parameters.size());
            std::memcpy(reinterpret_cast<void*>(render+0x110),transforms.data(),transforms.size());
            std::memcpy(reinterpret_cast<void*>(render+0x1d0),parameters.data(),parameters.size());
            *reinterpret_cast<std::uint8_t*>(render+0x1dc)=flags;
            return Matches(base);
        } __except(1) {return false;}
    }
    static bool PublishSet(std::uintptr_t base,std::span<const ReplayStaticVectorField> rows,
        bool gpu_retired,bool undo_retained) noexcept {
        if(!gpu_retired || !undo_retained)return false;
        for(const auto& row:rows)if(!row.Bound(base))return false;
        for(const auto& row:rows)if(!row.Publish(base))return false;
        return true;
    }
private:
    template<class T> static T Read(std::uintptr_t p) noexcept {return *reinterpret_cast<const T*>(p);}
};
struct ReplayStaticVectorFieldSet {
    std::array<ReplayStaticVectorField,64> rows{};
    std::size_t count{};
    bool captured{};
};
}
