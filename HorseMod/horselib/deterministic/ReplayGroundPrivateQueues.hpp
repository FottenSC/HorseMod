#pragma once
#include <cstdint>
#include <cstddef>
#include <span>

namespace Horse::Deterministic {
// Consumers modified by1420289D0 and142028750. A private actor may have a
// scene identifier for retirement without belonging to these engine lists.
struct ReplayGroundPrivateQueues {
    struct Binding {std::uintptr_t actor{},body{};};
    struct Array {std::uintptr_t data{};int count{},capacity{};};
    template<class Read> static bool Absent(Read read,std::uintptr_t owner,
        std::span<const Binding> bindings) noexcept {
        std::uintptr_t pending{};unsigned char asynchronous{};
        if(!owner || !read(owner+0x160,pending) || pending || !read(owner,asynchronous))return false;
        for(unsigned offset:{0x198u,0x1e0u,0x228u})for(unsigned side:{0x10u,0x20u}) {
            int count{};if(!read(owner+offset+side,count) || count)return false;
        }
        for(unsigned offset:{0x370u,0x380u}) {
            if(offset==0x380 && !asynchronous)continue;
            std::uintptr_t substep{};if(!read(owner+offset,substep) || !substep)return false;
            for(unsigned side:{0u,0x50u}) {
                int count{},free{};
                if(!read(substep+side+8,count) || !read(substep+side+0x34,free)
                    || count<0 || count>4096 || count!=free)return false;
            }
        }
        for(unsigned offset:{0x310u,0x330u}) {
            Array list{};
            if(!read(owner+offset,list) || list.count<0 || list.capacity<list.count || list.capacity>8192
                || (list.capacity?!list.data:bool(list.data)) || list.data>UINTPTR_MAX-std::size_t(list.capacity)*8)return false;
            for(int i=0;i<list.count;++i) {
                std::uintptr_t body{};if(!read(list.data+i*8,body))return false;
                for(const auto& b:bindings)if(body && body==b.body)return false;
            }
        }
        for(unsigned offset:{0x3f0u,0x490u}) {
            const auto map=owner+offset;int count{},free{},bits{},capacity{};
            std::uintptr_t data{},heap{};
            if(!read(map+8,count) || !read(map+0x34,free) || !read(map+0x28,bits)
                || !read(map+0x2c,capacity) || !read(map,data) || !read(map+0x20,heap)
                || count<0 || count>4096 || free<0 || free>count || bits!=count || capacity<bits
                || (count && !data))return false;
            unsigned occupied{};
            for(int i=0;i<count;++i) {
                unsigned flags{};if(!read((heap?heap:map+0x10)+(i/32)*4,flags))return false;
                if(!((flags>>(i%32))&1))continue;
                ++occupied;std::uintptr_t actor{};if(!read(data+i*0x18,actor))return false;
                for(const auto& b:bindings)if(actor && actor==b.actor)return false;
            }
            if(occupied!=unsigned(count-free))return false;
        }
        return true;
    }
};
}
