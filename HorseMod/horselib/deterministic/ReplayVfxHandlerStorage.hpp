#pragma once
#include <Windows.h>
#include <array>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cstddef>
namespace Horse::Deterministic {
class ReplayVfxHandlerStorage final {
    template<class T> static T Read(const void* p,std::size_t offset=0) {T v{};std::memcpy(&v,static_cast<const std::byte*>(p)+offset,sizeof(v));return v;}
    static bool Copy(void* to,const void* from,std::size_t n) noexcept {
        __try {if(n)std::memcpy(to,from,n);return true;} __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool Equal(const void* a,const void* b,std::size_t n) noexcept {
        __try {return !n || !std::memcmp(a,b,n);} __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    struct Node {std::byte* address{};std::vector<std::byte> bytes;int parent{-1};std::size_t offset{};};
public:
    struct Graph {
        static constexpr std::size_t limit=256;
        std::array<Node,limit> nodes{};std::size_t count{},budget{};
        int rejection{};bool Reject(int code){rejection=code;return false;}
        // Failed reads can allocate a node without publishing count. Such
        // scratch remains owned and must still be charged.
        std::size_t owned_bytes() const {std::size_t n=sizeof(*this);for(const auto& node:nodes)n+=node.bytes.capacity();return n;}
        int Add(void* p,std::size_t bytes,int parent=-1,std::size_t offset=0) {
            if(!bytes) return p?-2:-1;
            if(!p || count==limit || bytes>budget || owned_bytes()>budget-bytes) return -2;
            const auto a=reinterpret_cast<std::uintptr_t>(p);
            if(bytes>UINTPTR_MAX-a) return -2;
            for(std::size_t i=0;i<count;++i) {
                const auto b=reinterpret_cast<std::uintptr_t>(nodes[i].address);
                if(a<b+nodes[i].bytes.size() && b<a+bytes) return -2;
            }
            auto& node=nodes[count];node.address=static_cast<std::byte*>(p);node.parent=parent;node.offset=offset;
            node.bytes.resize(bytes);if(!Copy(node.bytes.data(),p,bytes))return -2;
            return static_cast<int>(count++);
        }
        int Array(int parent,std::size_t offset,std::size_t stride,int maximum) {
            const auto* header=nodes[parent].bytes.data()+offset;
            const auto n=Read<int>(header,8),cap=Read<int>(header,12);
            if(n<0 || cap<n || cap>maximum) return -2;
            return Add(Read<void*>(header),std::size_t(cap)*stride,parent,offset);
        }
        bool Map(int parent,std::size_t offset,bool slot_arrays,int maximum=1024) {
            const auto* h=nodes[parent].bytes.data()+offset;
            const int n=Read<int>(h,8),cap=Read<int>(h,12),bits=Read<int>(h,0x28),maxbits=Read<int>(h,0x2c),free=Read<int>(h,0x34),hashn=Read<int>(h,0x48);
            if(n<0 || cap<n || maximum>4096 || cap>maximum || bits!=n || maxbits<bits || maxbits>maximum*2
                || free<0 || free>n || hashn<0 || hashn>maximum*2 || (hashn && (hashn&(hashn-1)))) return Reject(1);
            const auto data=Array(parent,offset,32,maximum);if(data==-2)return Reject(2);
            const auto* flags=Read<const unsigned*>(h,0x20);
            if(flags) {if(Add(const_cast<unsigned*>(flags),std::size_t((maxbits+31)/32)*4,parent,offset+0x20)<0)return Reject(3);}
            else {if(maxbits>128)return Reject(4);flags=reinterpret_cast<const unsigned*>(h+0x10);}
            const auto* hash=Read<const int*>(h,0x40);
            if(hash) {if(Add(const_cast<int*>(hash),std::size_t(hashn)*4,parent,offset+0x40)<0)return Reject(5);}
            else {if(hashn>2)return Reject(6);hash=reinterpret_cast<const int*>(h+0x38);}
            std::array<bool,4096> visited{};int active=0;
            for(int bucket=0;bucket<hashn;++bucket) {
                int i=hash[bucket];
                while(i!=-1) {
                    if(i<0 || i>=n || visited[i] || !(flags[i/32]&(1u<<(i%32))) || data<0)return Reject(7);
                    visited[i]=true;++active;
                    const auto* row=nodes[data].bytes.data()+std::size_t(i)*32;
                    if(Read<int>(row,28)!=bucket)return Reject(8);
                    i=Read<int>(row,24);
                }
            }
            if(active!=n-free)return Reject(9);
            for(int i=0;i<n;++i) {
                if(bool(flags[i/32]&(1u<<(i%32)))!=visited[i])return Reject(10);
                if(visited[i] && slot_arrays && Array(data,std::size_t(i)*32+8,4,4096)==-2)return Reject(11);
            }
            return true;
        }
        bool Matches(const std::array<void*,limit>* installed=nullptr) const {
            for(std::size_t i=0;i<count;++i) {
                const auto& n=nodes[i];const auto* live=static_cast<const std::byte*>(installed?(*installed)[i]:n.address);
                // Pointer relocation is the sole permitted byte difference.
                std::size_t cursor=0;
                while(cursor<n.bytes.size()) {
                    const Node* child=nullptr;std::size_t child_index{};
                    for(std::size_t j=0;j<count;++j) if(nodes[j].parent==int(i) && nodes[j].offset>=cursor
                        && (!child || nodes[j].offset<child->offset)){child=&nodes[j];child_index=j;}
                    const auto end=child?child->offset:n.bytes.size();
                    if(!Equal(live+cursor,n.bytes.data()+cursor,end-cursor))return false;
                    if(!child)break;
                    const auto pointer=installed?(*installed)[child_index]:child->address;
                    if(!Equal(live+end,&pointer,8))return false;
                    cursor=end+8;
                }
            }
            return true;
        }
        bool Runtime(void* root) {
            if(count || Add(root,0x88)!=0)return false;
            const auto groups=Array(0,0,16,128);if(groups==-2)return false;
            if(groups>=0)for(int i=0;i<Read<int>(nodes[0].bytes.data(),8);++i)
                if(Array(groups,std::size_t(i)*16,4,4096)==-2)return false;
            return Map(0,0x10,true) && Array(0,0x60,2,4096)!=-2 && Array(0,0x78,4,4096)!=-2;
        }
        // Roots are the same inline native object. All separately allocated
        // buffers must be disjoint before either graph can be retired.
        bool AllocationsDisjoint(const Graph& other) const {
            if(!count || !other.count)return false;
            for(std::size_t i=1;i<count;++i)for(std::size_t j=1;j<other.count;++j) {
                const auto a=reinterpret_cast<std::uintptr_t>(nodes[i].address);
                const auto b=reinterpret_cast<std::uintptr_t>(other.nodes[j].address);
                if(nodes[i].bytes.size()>UINTPTR_MAX-a || other.nodes[j].bytes.size()>UINTPTR_MAX-b
                    || (a<b+other.nodes[j].bytes.size() && b<a+nodes[i].bytes.size()))return false;
            }
            return true;
        }
        bool AllocatedValuesMatch() const {
            for(std::size_t i=1;i<count;++i)
                if(!Equal(nodes[i].address,nodes[i].bytes.data(),nodes[i].bytes.size()))return false;
            return true;
        }
    };
};
}
