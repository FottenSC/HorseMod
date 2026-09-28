#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

namespace ReplayQualification {
struct ReceiverCpuSample {
    int count{};
    std::uint64_t hash{14695981039346656037ull};
    // Native141FA0730 initializes positions at10, base velocity20 and
    // velocity30.141FA0360 integrates30 into10;141FA9E60 consumes life0C.
    // Observe these semantic values and the birth counter, never addresses,
    // inactive allocation slots or unknown per-module payload bytes.
    template<class Read> bool Capture(Read&& read,std::uintptr_t emitter) {
        *this={};int capacity{},stride{};std::uintptr_t data{},indices{};
        std::uint32_t births{},time{};
        if(!emitter || !read(emitter+0x118,count) || !read(emitter+0x120,capacity)
            || !read(emitter+0x114,stride) || !read(emitter+0xf0,data) || !read(emitter+0xf8,indices)
            || !read(emitter+0x11c,births) || !read(emitter+0x12c,time)
            || count<0 || count>4096 || capacity<count || capacity>65536
            || (count && (!data || !indices || stride<0x40 || stride>4096)))return false;
        const auto add=[&](std::uint32_t word) {
            for(unsigned shift=0;shift<32;shift+=8)hash=(hash^((word>>shift)&255u))*1099511628211ull;
        };
        add(static_cast<unsigned>(count));add(births);add(time);
        for(int i=0;i<count;++i) {
            std::uint16_t index{};std::array<std::uint32_t,16> row{};
            if(!read(indices+std::size_t(i)*2,index) || index>=capacity
                || !read(data+std::size_t(index)*std::size_t(stride),row))return false;
            for(const auto word:{3u,4u,5u,6u,8u,9u,10u,12u,13u,14u})add(row[word]);
        }
        return true;
    }
};
}
