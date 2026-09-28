#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Horse::Deterministic {
// Operation-local undo for 141F711A0's render recreation side effect.
// Bit200 is consumed by 141FAAF90 to reset mesh previous-position state;
// it must return to its actual pre-call value before any simulation resumes.
struct ReplayStageParticleAdmission {
    std::uint32_t original{};
    bool retained{};
    bool Retain(std::uint32_t flags) noexcept {
        if(flags&0x80) return false; // 141F71FD0 would finalize emitter storage.
        if(retained) return Accepts(flags);
        original=flags;retained=true;return true;
    }
    bool Accepts(std::uint32_t flags) const noexcept {
        return retained && (flags==original || flags==(original|0x200u));
    }
};
// Native source values, not a render-proxy image. Member/weak identities are
// process-local bindings; they never become historical pointer writes.
struct ReplayStageVisibility {
    enum class Kind : std::uint8_t { Mesh, Wall, Emitter };
    struct Values {
        std::uint8_t requested{}, break_state{};
        float alpha{}, rate{};
        friend bool operator==(const Values&, const Values&) = default;
    } values;
    struct Component {
        std::uintptr_t object{}, type{}, parent{}, asset{};
        std::array<std::int32_t,2> weak{};
        std::uint32_t visibility{}, flags{}, primitive_id{};
        std::uint16_t member_offset{}, asset_offset{};
        std::uintptr_t registration_entry{};
        std::uint8_t primary_tick_flags{};
        bool base_registration{};
        std::uint8_t secondary_tick_flags{};
        bool primitive_registration{};
        bool DormantRegistration() const noexcept { return base_registration && (flags&1) && !(primary_tick_flags&0x42); }
        std::uintptr_t materials{}, children{};
        std::array<std::uintptr_t,16> mids{}, child_objects{};
        std::array<std::array<std::int32_t,2>,16> mid_weak{};
        std::int32_t material_count{},material_capacity{},child_count{},child_capacity{};
        friend bool operator==(const Component&, const Component&) = default;
    };
    std::uintptr_t actor{}, type{}, root{}, effect{};
    std::array<std::int32_t,2> weak{};
    std::array<Component,6> components{};
    // Binding witness only. Dispatch/listener storage is not restored by the
    // visibility owner; changed event ownership remains unsupported.
    std::array<std::byte,0x68> wall_event{};
    std::int32_t fade_frames{};
    std::uint8_t propagation{}, enabled{}, component_count{};
    Kind kind{};

    static bool Classify(std::uintptr_t base, std::uintptr_t table, Kind& kind) noexcept {
        if(table==base+0x32d2368) kind=Kind::Mesh;
        else if(table==base+0x32cfe40) kind=Kind::Wall;
        else if(table==base+0x335d518) kind=Kind::Emitter;
        else return false;
        return true;
    }
    bool SupportedValues() const noexcept {
        if(values.requested>2 || propagation>1) return false;
        if(kind==Kind::Emitter) return true;
        if(fade_frames<=0 || fade_frames>100000 || enabled>1
            || !std::isfinite(values.alpha) || !std::isfinite(values.rate)
            || values.alpha<0 || values.alpha>1 || std::abs(values.rate)>1) return false;
        // State1 consumes an independently owned skeletal animation before
        // state2 publication (140562090). Do not admit it as a scalar restore.
        return kind!=Kind::Wall || values.break_state==0 || values.break_state==2;
    }
    bool SameBinding(const ReplayStageVisibility& other, unsigned owned_registration=0) const noexcept {
        if(actor!=other.actor || type!=other.type || weak!=other.weak || root!=other.root
            || effect!=other.effect || kind!=other.kind || propagation!=other.propagation
            || wall_event!=other.wall_event
            || enabled!=other.enabled || fade_frames!=other.fade_frames
            // Break transitions also own animation/event/effect work. This
            // visibility transaction does not replace that lifecycle.
            || (kind==Kind::Wall && values.break_state!=other.values.break_state)
            || component_count!=other.component_count) return false;
        for(unsigned i=0;i<component_count;++i) {
            auto a=components[i], b=other.components[i];
            a.visibility&=~0x10u;b.visibility&=~0x10u;
            if(a.DormantRegistration() && b.DormantRegistration()) {a.flags&=~0x20000000u;b.flags&=~0x20000000u;}
            if((owned_registration&(1u<<i)) && (a.base_registration || a.primitive_registration)
                && a.registration_entry==b.registration_entry) {
                a.flags&=~0x20000000u;b.flags&=~0x20000000u;
                a.primary_tick_flags&=~0x40u;b.primary_tick_flags&=~0x40u;
                a.secondary_tick_flags&=~0x40u;b.secondary_tick_flags&=~0x40u;
            }
            a.flags&=~0xc0000060u;b.flags&=~0xc0000060u;
            if(a!=b) return false;
        }
        return true;
    }
    template<class Read, class Weak>
    bool ReadFrom(std::uintptr_t base, std::uintptr_t object, Read read, Weak make_weak) noexcept {
        *this={};actor=object;
        if(!read(actor,type) || !Classify(base,type,kind) || !make_weak(actor,weak)
            || !read(actor+0x168,root) || !read(actor+0x388,propagation)
            || !read(actor+0x389,values.requested)) return false;
        if(kind==Kind::Mesh) {
            component_count=3;
            if(!read(actor+0x3c0,fade_frames) || !read(actor+0x3c4,enabled)
                || !read(actor+0x3c8,values.alpha) || !read(actor+0x3cc,values.rate)) return false;
        } else if(kind==Kind::Wall) {
            component_count=6;enabled=1;
            if(!read(actor+0x454,fade_frames) || !read(actor+0x468,values.break_state)
                || !read(actor+0x46c,values.alpha) || !read(actor+0x470,values.rate)
                || !read(actor+0x460,effect) || !read(actor+0x3b0,wall_event)) return false;
            std::int32_t dispatch_depth{};
            if(!read(actor+0x414,dispatch_depth) || dispatch_depth)return false;
        } else component_count=1;
        for(unsigned i=0;i<component_count;++i) {
            auto& c=components[i];
            c.member_offset=kind==Kind::Mesh?0x3a8+i*8:kind==Kind::Wall?0x420+i*8:0x168;
            c.asset_offset=kind==Kind::Emitter?0x808:kind==Kind::Wall && i>=4?0x910:0x920;
            if(!read(actor+c.member_offset,c.object)) return false;
            if(!c.object) continue;
            std::uintptr_t owner{};
            if(!make_weak(c.object,c.weak) || !read(c.object,c.type)
                || (kind==Kind::Emitter && c.type!=base+0x335db28)
                || !read(c.object+0x190,owner) || owner!=actor || !read(c.object+0x1d0,c.parent)
                || !read(c.object+c.asset_offset,c.asset) || !read(c.object+0x240,c.visibility)
                || !read(c.object+0x188,c.flags) || !read(c.object+0x420,c.primitive_id)) return false;
            if(!read(c.type+0x2d8,c.registration_entry) || !read(c.object+0x11c,c.primary_tick_flags))return false;
            c.base_registration=c.registration_entry==base+0x1d58ac0;
            c.primitive_registration=c.registration_entry==base+0x1da9000;
            if(c.primitive_registration && !read(c.object+0x7bc,c.secondary_tick_flags))return false;
            if(!read(c.object+0x1d8,c.children) || !read(c.object+0x1e0,c.child_count)
                || !read(c.object+0x1e4,c.child_capacity) || c.child_count<0 || c.child_count>16
                || c.child_capacity<c.child_count || (c.child_count && !c.children))return false;
            for(int n=0;n<c.child_count;++n)if(!read(c.children+n*8,c.child_objects[n]))return false;
            const auto material_offset=kind==Kind::Mesh && i==1?0x9a8:
                kind==Kind::Wall && (i==1 || i==3)?0x9a8:kind==Kind::Wall && i==5?0xfe8:0;
            if(material_offset) {
                if(!read(c.object+material_offset,c.materials) || !read(c.object+material_offset+8,c.material_count)
                    || !read(c.object+material_offset+12,c.material_capacity) || c.material_count<0 || c.material_count>16
                    || c.material_capacity<c.material_count || (c.material_count && !c.materials))return false;
                for(int n=0;n<c.material_count;++n)
                    if(!read(c.materials+n*8,c.mids[n]) || (c.mids[n] && !make_weak(c.mids[n],c.mid_weak[n])))return false;
            }
            for(unsigned j=0;j<i;++j) if(components[j].object==c.object) return false;
        }
        return SupportedValues();
    }
    template<class Write>
    bool WriteValues(Write write) const noexcept {
        if(!SupportedValues() || !write(actor+0x389,values.requested)) return false;
        if(kind==Kind::Emitter) return true;
        if(kind==Kind::Mesh)
            return write(actor+0x3c8,values.alpha) && write(actor+0x3cc,values.rate);
        return write(actor+0x468,values.break_state) && write(actor+0x46c,values.alpha)
            && write(actor+0x470,values.rate);
    }
};

// One operation's publication plan. B remains the retained owner; only A's
// requested visibility is added. No historical native destination is kept.
struct ReplayStageRenderOwner {
    ReplayStageVisibility original;
    std::array<std::uint32_t,6> target_visibility{};
    bool Matches(const ReplayStageVisibility& target,const ReplayStageVisibility& before) const noexcept {
        if(!original.SameBinding(before) || !target.SameBinding(before) || original.values!=before.values)return false;
        for(unsigned i=0;i<target_visibility.size();++i)
            if(target_visibility[i]!=target.components[i].visibility
                || original.components[i].visibility!=before.components[i].visibility
                || original.components[i].flags!=before.components[i].flags)return false;
        return true;
    }
    template<class Read,class Resolve>
    bool TargetDestination(std::uintptr_t base,std::uintptr_t component,
        const std::array<std::int32_t,2>& weak,unsigned id,std::uintptr_t proxy_type,
        unsigned slots,std::uintptr_t mesh,std::uintptr_t render_data,Read read,Resolve resolve) const noexcept {
        if(original.kind==ReplayStageVisibility::Kind::Emitter || !original.SupportedValues()
            || original.component_count>original.components.size() || proxy_type!=base+0x36bd1a8
            || !slots || slots>17 || !mesh || !render_data)return false;
        const auto same=[&]<class T>(std::uintptr_t p,const T& expected) {
            T value{};return read(p,value) && value==expected;
        };
        for(unsigned i=0;i<original.component_count;++i) {
            const auto& c=original.components[i];if(c.object!=component)continue;
            if(c.weak!=weak || c.primitive_id!=id || c.asset_offset!=0x920 || c.asset!=mesh
                || (c.visibility&0x10) || !(target_visibility[i]&0x10)
                || (c.visibility&~0x10u)!=(target_visibility[i]&~0x10u)
                || (c.flags&0xc0000060u) || resolve(c.weak)!=component || resolve(original.weak)!=original.actor
                || !same(original.actor,original.type) || !same(original.actor+c.member_offset,component)
                || !same(component,c.type) || !same(component+0x190,original.actor)
                || !same(component+0x1d0,c.parent) || !same(component+0x920,mesh)
                || !same(mesh+0x38,render_data) || !same(component+0x420,id)
                || !same(component+0x790,std::uintptr_t{}))return false;
            unsigned visibility{},flags{};
            if(!read(component+0x240,visibility) || !read(component+0x188,flags)
                || (flags&~0xc0000060u)!=(c.flags&~0xc0000060u))return false;
            // Before publication B is unchanged. After publication native
            // MarkRenderStateDirty has queued A's recreate. The world owner
            // independently validates that weak-set membership and epoch.
            if(visibility==c.visibility)return !(flags&0xc0000060u);
            return visibility==target_visibility[i] && (flags&0x20u) && (flags>>30);
        }
        return false;
    }
};
}
