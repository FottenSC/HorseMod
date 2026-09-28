#pragma once
#include <array>
#include <cstdint>

namespace Horse::Deterministic {
// Independent comparison data. This subset witnesses shared slot allocation,
// root state, controller/fade timing and recipe scalars. It does not replace
// lifetime admission, capture ownership or grant motion exclusion.
struct ReplayGroundLifecycleObservation {
    unsigned roots{},next_id{};
    std::uint64_t hash{14695981039346656037ull};
    friend bool operator==(const ReplayGroundLifecycleObservation&,const ReplayGroundLifecycleObservation&)=default;
    template<class Read,class Live> bool Capture(Read read,Live live,
        std::uintptr_t base,std::uintptr_t battle) noexcept {
        *this={};
        const auto add=[&](std::uint32_t v) {
            for(unsigned i=0;i<4;++i)hash=(hash^((v>>(8*i))&255))*1099511628211ull;
        };
        std::uintptr_t manager{},type{};
        if(!battle || !read(battle+0x508,manager))return false;
        add(manager?1:0);
        if(!manager)return true; // Native setup before the manager exists.
        struct Array {std::uintptr_t data{};int count{},capacity{};} slots;
        if(!live(manager) || !read(manager,type) || type!=base+0x3356f68
            || !read(manager+0x3e0,next_id) || !read(manager+0x3f8,slots)
            || slots.count<0 || slots.count>4096 || slots.capacity<slots.count
            || (slots.count&&!slots.data))return false;
        add(next_id);add(static_cast<unsigned>(slots.count));
        for(int i=0;i<slots.count;++i) {
            std::uintptr_t root{},controller{};unsigned id{};
            if(!read(slots.data+std::uintptr_t(i)*0xc0,root)
                || !read(slots.data+std::uintptr_t(i)*0xc0+8,id))return false;
            add(id);add(root?1:0);
            if(!root)continue;
            if(++roots>64 || !live(root) || !read(root,type) || type!=base+0x33566f8)return false;
            unsigned char mode{},auto_destroy{};Array ring{};
            std::array<unsigned,12> transform{};std::array<unsigned,13> recipe{};
            std::array<unsigned,3> clock{};
            if(!read(root+0x830,mode) || !read(root+0x808,auto_destroy)
                || !read(root+0x820,ring) || ring.count<0 || ring.count>16 || ring.capacity<ring.count
                || (ring.count&&!ring.data) || !read(root+0x850,recipe)
                || !read(root+0xa08,clock) || !read(root+0x270,transform)
                || !read(root+0x8b0,controller))return false;
            add(mode);add(auto_destroy);add(static_cast<unsigned>(ring.count));
            // Recipe+30..63 includes fade request/duration/elapsed. All are
            // scalar native state; no object addresses or child motion here.
            for(unsigned j=0;j<9;++j)add(recipe[j]);
            add(recipe[9]&255);add((recipe[9]>>8)&255);add(recipe[10]&255);
            add(recipe[11]);add(recipe[12]); // Exclude native scalar padding.
            for(auto v:clock)add(v);
            for(unsigned lane:{0u,1u,2u,3u,4u,5u,6u,8u,9u,10u})add(transform[lane]);
            add(controller?1:0);
            if(controller) {
                std::uintptr_t callable_type{},context{},callback{};
                if(!read(controller,callable_type) || callable_type!=base+0x3510a68
                    || !read(controller+8,context) || context!=root+0x820
                    || !read(controller+16,callback) || callback<base || callback-base>0x6000000)return false;
                add(static_cast<unsigned>(callback-base));
            }
        }
        return true;
    }
};
}
