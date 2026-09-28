#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace Horse::Deterministic {
// An operation-local copy of the native +1020 map. The native erase callback
// owns query-reference release; no scene/component pointer is present in a key.
struct ReplayOcclusionHistory {
    template<class T> static T Read(const void* p,std::size_t o=0) noexcept {
        T v;std::memcpy(&v,static_cast<const std::byte*>(p)+o,sizeof(v));return v;
    }
    template<class T> static void Write(void* p,std::size_t o,T v) noexcept {
        std::memcpy(static_cast<std::byte*>(p)+o,&v,sizeof(v));
    }
    struct Result {bool ok{};unsigned removed{},queries_released{};};
    template<class Orphan,class Erase>
    static Result Prune(std::array<std::byte,0x50>& header,std::span<std::byte> entries,
        std::span<std::byte> flags,std::span<std::byte> hashes,std::vector<void*>& queries,
        std::size_t& leases,Orphan orphan,Erase erase) noexcept {
        const int count=Read<int>(header.data(),8),free=Read<int>(header.data(),0x34);
        const int buckets=Read<int>(header.data(),0x48),bits=Read<int>(header.data(),0x2c);
        if(count<0 || count>8192 || free<0 || free>count || buckets<1 || buckets>16384
            || (buckets&(buckets-1)) || bits<count || bits>8192 || bits%32
            || Read<int>(header.data(),0x28)!=count || Read<int>(header.data(),0xc)<count
            || (!Read<std::uintptr_t>(header.data(),0x20) && flags.size()>16)
            || (!Read<std::uintptr_t>(header.data(),0x40) && hashes.size()!=4)
            || entries.size()!=std::size_t(count)*0x50 || flags.size()!=std::size_t(bits)/8
            || hashes.size()!=std::size_t(buckets)*4 || leases!=queries.size()) return {};
        const auto active=[&](int i){return i>=0 && i<count
            && (Read<unsigned>(flags.data(),std::size_t(i/32)*4)&(1u<<(i%32)));};
        std::array<bool,8192> seen{},remove{};
        unsigned occupied{},planned{};std::size_t query_count{};
        for(int bucket=0;bucket<buckets;++bucket) {
            int i=Read<int>(hashes.data(),std::size_t(bucket)*4);
            while(i!=-1) {
                if(!active(i) || seen[i]) return {};
                seen[i]=true;const auto* row=entries.data()+std::size_t(i)*0x50;
                if((Read<unsigned>(row,0x4c)&unsigned(buckets-1))!=unsigned(bucket)) return {};
                i=Read<int>(row,0x48);
            }
        }
        for(int i=0;i<count;++i) {
            if(!active(i)) {if(seen[i]) return {};continue;}
            if(!seen[i]) return {};
            ++occupied;const auto* row=entries.data()+std::size_t(i)*0x50;
            const int n=Read<int>(row,0x20);
            if(Read<std::uintptr_t>(row,0x18) || n<1 || n>2 || Read<int>(row,0x24)!=2) return {};
            for(int j=0;j<n;++j) if(auto* q=Read<void*>(row,8+std::size_t(j)*8)) {
                if(query_count>=queries.size() || queries[query_count++]!=q) return {};
            }
            remove[i]=orphan(Read<unsigned>(row),Read<unsigned>(row,0x40));planned+=remove[i];
        }
        if(occupied!=unsigned(count-free) || query_count!=queries.size()) return {};
        int head=Read<int>(header.data(),0x30),previous=-1;
        for(int n=0;n<free;++n) {
            if(head<0 || head>=count || active(head) || seen[head]) return {};
            seen[head]=true;const auto* row=entries.data()+std::size_t(head)*0x50;
            if(Read<int>(row)!=previous) return {};
            previous=head;head=Read<int>(row,4);
        }
        if(head!=-1) return {};
        if(!planned) return {true};
        auto local=header;
        Write(local.data(),0,entries.data());Write(local.data(),0x20,flags.data());Write(local.data(),0x40,hashes.data());
        unsigned released{};
        for(int i=0;i<count;++i) if(remove[i]) {
            const auto* row=entries.data()+std::size_t(i)*0x50;
            for(int j=0;j<Read<int>(row,0x20);++j) released+=Read<void*>(row,8+std::size_t(j)*8)!=nullptr;
            // Native1414F5290 unlinks the hash chain;1414F5780 drops each
            // query reference;141D01C40 links the free slot without allocating.
            erase(local.data(),i);
        }
        std::size_t kept{};
        for(int i=0;i<count;++i) if(active(i)) {
            const auto* row=entries.data()+std::size_t(i)*0x50;
            for(int j=0;j<Read<int>(row,0x20);++j)
                if(auto* q=Read<void*>(row,8+std::size_t(j)*8)) queries[kept++]=q;
        }
        queries.resize(kept);leases=kept;
        for(auto offset:{0u,0x20u,0x40u})
            Write(local.data(),offset,Read<std::uintptr_t>(header.data(),offset));
        if(!Read<std::uintptr_t>(header.data(),0x20)) std::memcpy(local.data()+0x10,flags.data(),flags.size());
        if(!Read<std::uintptr_t>(header.data(),0x40)) std::memcpy(local.data()+0x38,hashes.data(),hashes.size());
        header=local;
        return {true,planned,released};
    }
};
}
