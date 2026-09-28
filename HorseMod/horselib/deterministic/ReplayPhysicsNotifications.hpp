#pragma once

#include <array>
#include <cstdint>
#include <cstddef>

namespace Horse::Deterministic {
// Shipped PhysX ScScene notification continuation. Native EC190/EC240 append
// BodyCore keys; E7670 filters and dispatches them, then EE850 clears them.
// No callback, allocation, body activation or native lifetime change occurs here.
struct ReplayPhysicsNotifications {
    static constexpr unsigned max_entries=64, max_storage=256, max_buckets=512;
    struct Set {
        unsigned count{};
        std::array<std::uintptr_t,max_entries> cores{};
        friend bool operator==(const Set&,const Set&)=default;
    };
    std::uintptr_t scene{};
    std::array<Set,2> sets{}; // sleep, wake; dense order is dispatch order
    std::array<unsigned char,2> filtered{}; // +10F0 wake, +10F1 sleep
    struct Client {
        std::uintptr_t owner{},callback{},vtable{};
        friend bool operator==(const Client&,const Client&)=default;
    };
    std::uintptr_t client_storage{};
    unsigned client_count{};
    std::array<Client,16> clients{};
    bool valid{};
    friend bool operator==(const ReplayPhysicsNotifications&,const ReplayPhysicsNotifications&)=default;

    bool Empty() const noexcept {return !sets[0].count && !sets[1].count;}
    bool WellFormed() const noexcept {
        if(!valid || !scene || filtered[0]>1 || filtered[1]>1
            || !client_storage || !client_count || client_count>clients.size())return false;
        for(unsigned i=0;i<client_count;++i)
            if(!clients[i].owner || bool(clients[i].callback)!=bool(clients[i].vtable))return false;
        for(const auto& set:sets) {
            if(set.count>max_entries)return false;
            for(unsigned i=0;i<set.count;++i) {
                if(!set.cores[i])return false;
                for(unsigned j=0;j<i;++j)if(set.cores[j]==set.cores[i])return false;
            }
            for(unsigned i=set.count;i<max_entries;++i)if(set.cores[i])return false;
        }
        return true;
    }
    unsigned Membership(std::uintptr_t core) const noexcept {
        unsigned result{};
        for(unsigned slot=0;slot<2;++slot)
            for(unsigned i=0;i<sets[slot].count && i<max_entries;++i)
                if(sets[slot].cores[i]==core)result|=0x10u<<slot;
        return result;
    }
    // F9170/FF8C0 pointer hash, with native unsigned overflow semantics.
    static unsigned Hash(std::uintptr_t key) noexcept {
        std::uint64_t v=key+~(std::uint64_t(key)<<32);v^=v>>22;
        v+=~(v<<13);v=(v^(v>>8))*9;v^=v>>15;v+=~(v<<27);
        return unsigned(v>>31)^unsigned(v);
    }
    struct Storage {
        std::uintptr_t header{},allocation{},dense{},links{},buckets{};
        unsigned capacity{},bucket_count{},cursor{},epoch{},count{};
    };
    template<class Read> bool CaptureClients(const Read& read) noexcept {
        unsigned capacity{};
        if(!read(scene+0x10f8,client_storage) || !read(scene+0x1100,client_count)
            || !read(scene+0x1104,capacity) || !client_storage || !client_count
            || client_count>clients.size() || client_count>(capacity&0x7fffffffu))return false;
        for(unsigned i=0;i<client_count;++i) {
            auto& c=clients[i];
            if(!read(client_storage+i*8,c.owner) || !c.owner || !read(c.owner+0x48,c.callback)
                || (c.callback && !read(c.callback,c.vtable)))return false;
        }
        return true;
    }
    template<class Read> bool ClientsLive(const Read& read) const noexcept {
        ReplayPhysicsNotifications live;live.scene=scene;
        return live.CaptureClients(read) && live.client_storage==client_storage
            && live.client_count==client_count && live.clients==clients;
    }
    template<class Read> static bool ReadStorage(std::uintptr_t header,Storage& s,const Read& read) noexcept {
        s={};s.header=header;
        if(!read(header,s.allocation) || !read(header+8,s.dense) || !read(header+0x10,s.links)
            || !read(header+0x18,s.buckets) || !read(header+0x20,s.capacity)
            || !read(header+0x24,s.bucket_count) || !read(header+0x2c,s.cursor)
            || !read(header+0x30,s.epoch) || !read(header+0x34,s.count)
            || s.count>max_entries || s.count>s.capacity || s.capacity>max_storage
            || s.bucket_count>max_buckets)return false;
        if(!s.capacity)return !s.count && !s.bucket_count && !s.allocation && !s.dense
            && !s.links && !s.buckets && (s.cursor==0 || s.cursor==0xffffffffu);
        // FF8C0 owns one allocation: buckets, links, then 16-aligned dense keys.
        return s.bucket_count && !(s.bucket_count&(s.bucket_count-1))
            && s.allocation && !(s.allocation&15) && s.buckets==s.allocation
            && s.links==s.buckets+std::uintptr_t(s.bucket_count)*4
            && s.dense==((s.links+std::uintptr_t(s.capacity)*4+15)&~std::uintptr_t(15))
            && s.cursor==s.count;
    }
    template<class Read,class Owner> bool Capture(std::uintptr_t sc,const Read& read,const Owner& owns) noexcept {
        *this={};scene=sc;
        if(!scene || !read(scene+0x10f0,filtered) || !CaptureClients(read))return false;
        for(unsigned slot=0;slot<2;++slot) {
            Storage s;if(!ReadStorage(scene+(slot?0x10b8:0x1080),s,read))return false;
            auto& set=sets[slot];set.count=s.count;
            for(unsigned i=0;i<s.count;++i) {
                unsigned char client{};
                if(!read(s.dense+i*8,set.cores[i]) || !owns(set.cores[i])
                    || !read(set.cores[i]+0xb,client) || client>=client_count)return false;
            }
            std::array<bool,max_entries> seen{};unsigned reached{};
            for(unsigned bucket=0;bucket<s.bucket_count;++bucket) {
                unsigned index{};if(!read(s.buckets+bucket*4,index))return false;
                while(index!=0xffffffffu) {
                    if(index>=s.count || seen[index] || (Hash(set.cores[index])&(s.bucket_count-1))!=bucket)return false;
                    seen[index]=true;++reached;
                    if(!read(s.links+index*4,index))return false;
                }
            }
            if(reached!=s.count)return false;
        }
        valid=true;return WellFormed();
    }
    template<class Read,class Writable> bool CanWrite(const Read& read,const Writable& writable) const noexcept {
        if(!WellFormed() || !ClientsLive(read) || !writable(scene+0x10f0,2))return false;
        for(unsigned slot=0;slot<2;++slot) {
            Storage s;if(!ReadStorage(scene+(slot?0x10b8:0x1080),s,read)
                || sets[slot].count>s.capacity || !writable(s.header+0x2c,12)
                || (s.capacity && (!writable(s.dense,s.capacity*8) || !writable(s.links,s.capacity*4)
                    || !writable(s.buckets,s.bucket_count*4))))return false;
        }
        return true;
    }
    // Call under the enclosing physics hold/scene lock, after all owners,
    // current logical contents and destination ranges have passed preflight.
    // Backing pointers/capacity stay native. Mutation epochs advance, not rewind.
    template<class Read,class Writer> bool Write(const Read& read,const Writer& write) const noexcept {
        if(!WellFormed() || !ClientsLive(read))return false;
        for(unsigned slot=0;slot<2;++slot) {
            Storage s;if(!ReadStorage(scene+(slot?0x10b8:0x1080),s,read) || sets[slot].count>s.capacity)return false;
            std::array<unsigned,max_buckets> buckets{};buckets.fill(0xffffffffu);
            std::array<unsigned,max_storage> links{};
            for(unsigned i=0;i<s.capacity;++i)links[i]=i+1<s.capacity?i+1:0xffffffffu;
            for(unsigned i=0;i<sets[slot].count;++i) {
                const auto bucket=Hash(sets[slot].cores[i])&(s.bucket_count-1);
                links[i]=buckets[bucket];buckets[bucket]=i;
                if(!write(s.dense+i*8,sets[slot].cores[i]))return false;
            }
            for(unsigned i=0;i<s.bucket_count;++i)if(!write(s.buckets+i*4,buckets[i]))return false;
            for(unsigned i=0;i<s.capacity;++i)if(!write(s.links+i*4,links[i]))return false;
            const unsigned cursor=s.capacity?sets[slot].count:s.cursor;
            if(!write(s.header+0x2c,cursor) || !write(s.header+0x30,s.epoch+1)
                || !write(s.header+0x34,sets[slot].count))return false;
        }
        return write(scene+0x10f0,filtered);
    }
};
}
