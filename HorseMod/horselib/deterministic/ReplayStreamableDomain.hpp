#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse::Deterministic {
// Read-only native streamable admission. Caller supplies the GT/registry lock
// and memory-fault boundary. No handle promotion, resource pin or native call.
struct ReplayStreamableDomain {
    struct Diagnostic {unsigned check{};std::uintptr_t owner{};int count{};};
    template<class T> static T Read(const void* p,std::size_t offset=0) noexcept {
        T v;std::memcpy(&v,static_cast<const std::byte*>(p)+offset,sizeof(v));return v;
    }
    static void Hash(std::uint64_t& hash,std::uint64_t value) noexcept {
        for(unsigned i=0;i<8;++i) {hash^=(value>>(8*i))&255;hash*=1099511628211ull;}
    }
    static bool Handles(const void* array,bool weak,std::uint64_t& hash,unsigned& live,Diagnostic& diagnostic) noexcept {
        const int count=Read<int>(array,8),capacity=Read<int>(array,12);
        const auto* data=Read<const std::byte*>(array);
        diagnostic={35,reinterpret_cast<std::uintptr_t>(array),count};live=0;
        if(count<0 || count>65536 || capacity<count || (count&&!data)) return false;
        Hash(hash,count);
        for(int i=0;i<count;++i) {
            const auto* handle=Read<const void*>(data+i*16);
            const auto* control=Read<const void*>(data+i*16,8);
            Hash(hash,reinterpret_cast<std::uintptr_t>(handle));Hash(hash,reinterpret_cast<std::uintptr_t>(control));
            if(!control) {if(handle) return false;continue;}
            const int strong=Read<int>(control,8);
            if(strong<0 || Read<int>(control,12)<=0 || (!weak&&!strong)) return false;
            if(!strong) continue; // Dead payload may already be unmapped.
            if(!handle || (!Read<unsigned char>(handle,0x10) && !Read<unsigned char>(handle,0x12))) return false;
            ++live;
            Hash(hash,Read<unsigned char>(handle,0x10));Hash(hash,Read<unsigned char>(handle,0x12));
        }
        return true;
    }
    static bool Manager(const void* manager,std::uint64_t& hash,bool& relevant,Diagnostic& diagnostic) noexcept {
        relevant=false;diagnostic={33,reinterpret_cast<std::uintptr_t>(manager),0};
        const auto* map=static_cast<const std::byte*>(manager)+8;
        const int slots=Read<int>(map,8),capacity=Read<int>(map,12);
        const int bits=Read<int>(map,0x28),max_bits=Read<int>(map,0x2c),free=Read<int>(map,0x34);
        const auto* heap=Read<const unsigned*>(map,0x20);
        const auto* words=heap?heap:reinterpret_cast<const unsigned*>(map+0x10);
        const auto* data=Read<const std::byte*>(map);
        if(slots<0 || slots>65536 || capacity<slots || bits!=slots || max_bits<bits
            || (!heap&&max_bits>128) || free<0 || free>slots || (slots&&!data)) return false;
        int occupied{};
        for(int i=0;i<bits;++i) if(words[i/32]&(1u<<(i%32))) {
            ++occupied;const auto* record=Read<const void*>(data+i*32,0x10);
            diagnostic={34,reinterpret_cast<std::uintptr_t>(record),record?Read<int>(record,0x18):-1};
            if(!record || Read<unsigned char>(record,8) || Read<int>(record,0x18)!=0
                || Read<int>(record,0x1c)<0 || (Read<int>(record,0x1c)&&!Read<const void*>(record,0x10))) return false;
            unsigned live{};std::uint64_t handles=14695981039346656037ull;
            if(!Handles(static_cast<const std::byte*>(record)+0x20,true,handles,live,diagnostic)) return false;
            // Native14214BBF0 removes expired weak entries and destroys records
            // with no remaining handles. Under the no-load/no-waiter checks
            // above,14213F5F0 cannot invoke a callback for such a record.
            // Its cache address/GC reference is not a resumable callback lease.
            // Actual loaded assets remain subject to their consuming owners'
            // checkpoint binding checks; this never rewrites/pins cache state.
            if(!live) continue;
            relevant=true;Hash(hash,reinterpret_cast<std::uintptr_t>(record));
            Hash(hash,Read<std::uintptr_t>(record));Hash(hash,Read<unsigned char>(record,9));Hash(hash,handles);
        }
        unsigned managed{};
        if(occupied!=slots-free || !Handles(static_cast<const std::byte*>(manager)+0xa8,false,hash,managed,diagnostic)) return false;
        relevant|=managed!=0;
        return true;
    }
};
}
