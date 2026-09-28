#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <span>

namespace Horse::Deterministic {
// Pointer-free scene state consumed by141DA2820,1403D3030,141DB0600
// and141DA79D0. This is the same120-byte auxiliary layout already captured
// by the physics participant. Never repair only ComponentToWorld: the next
// native update consumes the relative values and rotation caches as well.
struct ReplayGroundPose {
    using World=std::array<std::byte,48>;
    using Auxiliary=std::array<std::byte,120>;
    // Qualification intervention only. The enclosing completed-boundary owner
    // must admit lifetime/physics/reader routes before calling this. Drive the
    // native movement entry so transforms, bodies and render dirty state agree;
    // never patch ComponentToWorld alone or import a control observation.
    // A partial failure reports the completed prefix and must not resume play.
    template<class Read,class Move> static bool PerturbNative(Read read,Move move,
        std::span<const std::uintptr_t> objects,unsigned& changed) noexcept {
        changed=0;
        if(objects.empty() || objects.size()>64)return false;
        alignas(16) std::array<std::array<float,12>,64> before{};
        for(std::size_t i=0;i<objects.size();++i) {
            if(!objects[i] || !read(objects[i]+0x270,before[i]))return false;
            for(std::size_t j=0;j<i;++j)if(objects[i]==objects[j])return false;
            for(unsigned lane:{0u,1u,2u,3u,4u,5u,6u,8u,9u,10u})
                if(!std::isfinite(before[i][lane]))return false;
        }
        for(std::size_t i=0;i<objects.size();++i) {
            const std::array<float,3> delta{i%2?-64.f:64.f,float(i%3)*16.f-16.f,32.f};
            alignas(16) std::array<float,4> rotation{};
            std::memcpy(rotation.data(),before[i].data(),16);
            if(!move(objects[i],delta,rotation))return false;
            std::array<float,12> after{};
            if(!read(objects[i]+0x270,after))return false;
            for(unsigned lane=0;lane<12;++lane) {
                if(lane==7 || lane==11)continue; // SIMD padding is not a value.
                const auto expected=before[i][lane]+(lane>=4 && lane<=6?delta[lane-4]:0.f);
                if(!std::isfinite(after[lane]) || std::fabs(after[lane]-expected)>.01f)return false;
            }
            ++changed;
        }
        return true;
    }
    template<unsigned Offset,unsigned Size,unsigned Cursor,class Read>
    static bool CaptureRange(Read read,std::uintptr_t object,Auxiliary& out) noexcept {
        std::array<std::byte,Size> value{};
        if(!read(object+Offset,value))return false;
        std::memcpy(out.data()+Cursor,value.data(),Size);return true;
    }
    template<class Read> static bool Capture(Read read,std::uintptr_t object,Auxiliary& out) noexcept {
        std::uintptr_t parent{};int children{},scopes{};
        if(!object || !read(object+0x1d0,parent) || parent || !read(object+0x1e0,children) || children
            || !read(object+0x3d8,scopes) || scopes)return false;
        return CaptureRange<0x24c,28,0>(read,object,out) && CaptureRange<0x2a0,28,28>(read,object,out)
            && CaptureRange<0x2c0,24,56>(read,object,out) && CaptureRange<0x2e0,28,80>(read,object,out)
            && CaptureRange<0x300,12,108>(read,object,out);
    }
    template<unsigned Offset,unsigned Size,unsigned Cursor,class Write>
    static bool InstallRange(Write write,std::uintptr_t object,const Auxiliary& source) noexcept {
        std::array<std::byte,Size> value{};std::memcpy(value.data(),source.data()+Cursor,Size);
        return write(object+Offset,value);
    }
    template<class Read,class Write> static bool InstallCold(Read read,Write write,std::uintptr_t object,
        const World& world,const Auxiliary& auxiliary,unsigned historical_flags) noexcept {
        unsigned flags{},current{};
        if(!object || !read(object+0x188,flags) || (flags&(7u|0x10000000u|0x20000000u))
            || !read(object+0x240,current))return false;
        for(unsigned offset:{0x1c8u,0x1d0u,0x128u,0x7c8u}) {
            std::uintptr_t owner{};if(!read(object+offset,owner) || owner)return false;
        }
        for(unsigned offset:{0x1e0u,0x3d8u,0x3c4u}) {
            int count{};if(!read(object+offset,count) || count)return false;
        }
        // These values came from the captured native producer, including its
        // bounds and lazy caches. No placement/RNG, Euler conversion, movement
        // epsilon or native propagation occurs while the new graph is private.
        return InstallRange<0x24c,28,0>(write,object,auxiliary)
            && InstallRange<0x2a0,28,28>(write,object,auxiliary)
            && InstallRange<0x2c0,24,56>(write,object,auxiliary)
            && InstallRange<0x2e0,28,80>(write,object,auxiliary)
            && InstallRange<0x300,12,108>(write,object,auxiliary)
            && write(object+0x270,world)
            && write(object+0x240,(current&~0xfu)|(historical_flags&0xfu));
    }
};
}
