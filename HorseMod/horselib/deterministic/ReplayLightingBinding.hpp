#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <span>

namespace Horse::Deterministic {
// Process-local semantic keys for an operation's relocated uniform slots.
// They validate correspondence; no historical proxy/LCI is dereferenced.
struct ReplayLightingBinding {
    struct PrimitiveIdReplacement {std::uint32_t source{},target{};};
    // Typed projection of cache+140's primitive-id -> allocation map. Native
    // 1412F6170/141308EA0 hash the uint key directly; 141629080 rebuilds in
    // ascending occupied-slot order, storing next/bucket at +10/+14. Preserve
    // allocation pointers, sparse holes and free-list bytes. No UObject ID or
    // global primitive counter is rewritten.
    static bool RebindPrimitiveAllocationKeys(std::span<std::byte> entries,unsigned count,
        std::span<const std::byte> flags,std::span<std::byte> hashes,
        std::span<const PrimitiveIdReplacement> replacements) noexcept {
        const auto read=[](const std::byte* p){std::uint32_t value;std::memcpy(&value,p,4);return value;};
        const auto write=[](std::byte* p,std::uint32_t value){std::memcpy(p,&value,4);};
        const auto buckets=hashes.size()/4;
        if(count>128 || entries.size()<std::size_t(count)*24 || flags.size()<(count+31u)/32u*4
            || hashes.size()%4 || !buckets || buckets>512 || (buckets&(buckets-1)) || replacements.size()>32) return false;
        for(std::size_t i=0;i<replacements.size();++i) {
            const auto& id=replacements[i];
            if(!id.source || !id.target || id.source==id.target)return false;
            for(std::size_t j=0;j<i;++j)if(replacements[j].source==id.source || replacements[j].target==id.target)return false;
        }
        const auto occupied=[&](unsigned i){return (read(flags.data()+(i/32)*4)>>(i%32))&1u;};
        const auto projected=[&](std::uint32_t key){for(const auto& id:replacements)if(id.source==key)return id.target;return key;};
        // Reject an inconsistent original chain instead of laundering it into
        // a valid projected map. All checks precede the first private write.
        std::array<bool,128> visited{};
        for(std::size_t bucket=0;bucket<buckets;++bucket) {
            auto index=read(hashes.data()+bucket*4);
            while(index!=UINT32_MAX) {
                if(index>=count || !occupied(index) || visited[index])return false;
                visited[index]=true;const auto* row=entries.data()+index*24;
                if((read(row)&(buckets-1))!=bucket || read(row+20)!=bucket)return false;
                index=read(row+16);
            }
        }
        for(unsigned i=0;i<count;++i) {
            if(bool(occupied(i))!=visited[i])return false;
            if(!visited[i])continue;
            const auto key=read(entries.data()+i*24);
            for(unsigned j=0;j<i;++j)if(visited[j]) {
                const auto other=read(entries.data()+j*24);
                if(key==other || projected(key)==projected(other))return false;
            }
        }
        for(std::size_t bucket=0;bucket<buckets;++bucket)write(hashes.data()+bucket*4,UINT32_MAX);
        for(unsigned i=0;i<count;++i)if(visited[i]) {
            auto* row=entries.data()+i*24;const auto key=projected(read(row));
            const auto bucket=key&std::uint32_t(buckets-1);
            write(row,key);write(row+16,read(hashes.data()+bucket*4));write(row+20,bucket);
            write(hashes.data()+bucket*4,i);
        }
        return true;
    }
    std::uintptr_t mesh{}, render_data{};
    std::array<std::array<std::uintptr_t,3>,16> lods{}; // mesh LOD, light map, shadow map
    unsigned count{};
    bool operator==(const ReplayLightingBinding&) const = default;
    bool Matches(const ReplayLightingBinding& other,unsigned slots) const noexcept {
        if(!mesh || !render_data || !count || count>lods.size() || slots!=count+1 || *this!=other) return false;
        for(unsigned i=0;i<count;++i) if(!lods[i][0]) return false;
        return true;
    }
};
}
