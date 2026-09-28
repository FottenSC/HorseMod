#include "ReplayGroundLifecycleObservation.hpp"
#include <array>
#include <cassert>
#include <cstring>
using Horse::Deterministic::ReplayGroundLifecycleObservation;
template<class T>void put(std::uintptr_t p,const T& value){std::memcpy(reinterpret_cast<void*>(p),&value,sizeof(value));}
int main(){
    std::array<std::byte,0x5000> bytes{};const auto b=reinterpret_cast<std::uintptr_t>(bytes.data());
    const auto battle=b,manager=b+0x800,root=b+0x1000,slots=b+0x3000,controller=b+0x3800;
    const std::uintptr_t base=0x140000000ull;
    struct Array{std::uintptr_t data;int count,capacity;};
    put(battle+0x508,manager);put(manager,base+0x3356f68);put(manager+0x3e0,8u);
    put(manager+0x3f8,Array{slots,1,1});put(slots,root);put(slots+8,7u);
    put(root,base+0x33566f8);put(root+0x820,Array{b+0x3400,5,5});put(root+0x830,static_cast<unsigned char>(2));
    put(root+0x8b0,controller);put(controller,base+0x3510a68);put(controller+8,root+0x820);put(controller+16,base+0x8a0230);
    const auto read=[&](std::uintptr_t p,auto& value){
        if(p<b || p+sizeof(value)>b+bytes.size())return false;
        std::memcpy(&value,reinterpret_cast<void*>(p),sizeof(value));return true;
    };
    const auto live=[&](auto object){return object==manager || object==root;};
    ReplayGroundLifecycleObservation first,changed;
    assert(first.Capture(read,live,base,battle) && first.roots==1 && first.next_id==8);
    // A visual child pose and padding cannot manufacture lifecycle divergence.
    put(b+0x3500,std::array<float,3>{100,200,300});put(root+0x876,static_cast<unsigned short>(0xbeef));
    assert(changed.Capture(read,live,base,battle) && changed==first);
    for(const auto offset:{0x830u,0x87cu,0x880u,0xa08u,0x280u}){
        unsigned before{};assert(read(root+offset,before));put(root+offset,before+1);
        assert(changed.Capture(read,live,base,battle) && changed.hash!=first.hash);put(root+offset,before);
    }
    put(manager+0x3e0,9u);assert(changed.Capture(read,live,base,battle) && changed!=first);put(manager+0x3e0,8u);
    put(slots+8,17u);assert(changed.Capture(read,live,base,battle) && changed.hash!=first.hash);put(slots+8,7u);
    put(controller+8,root);assert(!changed.Capture(read,live,base,battle));put(controller+8,root+0x820);
    put(manager+0x3f8,Array{slots,0,1});assert(changed.Capture(read,live,base,battle) && !changed.roots && changed.hash!=first.hash);
    put(battle+0x508,std::uintptr_t{});assert(changed.Capture(read,live,base,battle) && !changed.roots && !changed.next_id);
}
