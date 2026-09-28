#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse::Deterministic {
// Consumed CPU broadphase state, not an allocation image. Native tasks and
// per-update output buffers are deliberately outside this completed-boundary
// image. Unsupported owners never become a partially usable image.
struct ReplayPhysicsSapImage {
    static constexpr unsigned max_handles=512, max_pairs=128;
    static constexpr unsigned invalid=0x3fffffff;
    static bool ShapeAttributesSupported(unsigned attributes,unsigned filter_word3) noexcept {
        return attributes==0 || ((attributes==1 || attributes==0x11) && ((filter_word3>>21)&31)!=7);
    }
    struct Volume {
        std::uintptr_t shape{}, core{}, actor{};
        std::uint32_t group{}, contact_distance{};
        std::uint32_t attributes{},shape_id{},rigid_id{};
        bool empty_aggregate{};
        std::array<unsigned,4> filter_data{};
        std::array<std::uint32_t,6> bounds{};
        friend bool operator==(const Volume&,const Volume&)=default;
    };
    struct Pair {
        std::array<unsigned,2> handles{};
        std::uintptr_t interaction{};
        std::uint8_t state{};
        friend bool operator==(const Pair&,const Pair&)=default;
    };
    std::uintptr_t scene{},manager{},broadphase{},bounds_owner{},distance_owner{};
    unsigned extent{},boxes{},previous_boxes{},pair_count{};
    std::array<Volume,max_handles> volumes{};
    std::array<std::array<unsigned,max_handles/32>,3> pending{};
    std::array<std::array<std::array<unsigned,2>,max_handles>,3> box_endpoints{};
    std::array<std::array<unsigned,max_handles*2+2>,3> values{},owners{};
    std::array<Pair,max_pairs> pairs{};
    std::uint8_t bounds_changed{},origin_shifted{},persistent_changed{};
    bool valid{};
    std::uint8_t admission_step{}; // Failure diagnostic only; zero on a complete image.
    // Binding witnesses for native aggregates proven to have no members/work.
    // No aggregate allocation, timestamp, sort buffer or pair history is restored.
    struct EmptyAggregate {
        std::uintptr_t owner{},self{};
        unsigned slot{},handle{};
        friend bool operator==(const EmptyAggregate&,const EmptyAggregate&)=default;
    };
    struct IdContinuation {
        std::uintptr_t owner{};
        unsigned next{},count{};
        std::array<unsigned,max_handles> free{};
        friend bool operator==(const IdContinuation&,const IdContinuation&)=default;
    };
    // Logical ID continuation; complete active-owner coverage precedes writes.
    // Native backing and constraint IDs remain owned by native code.
    std::array<IdContinuation,4> ids{};
    // Live ShapeSim owners excluded from AABB processing by native115940.
    // These are immutable binding witnesses, not resurrected volumes.
    struct NonBroadphaseShape {
        std::uintptr_t shape{},core{},actor{};
        unsigned shape_id{},rigid_id{},flags{},contact_distance{},attributes{};
        std::array<unsigned,4> filter_data{};
        friend bool operator==(const NonBroadphaseShape&,const NonBroadphaseShape&)=default;
    };
    std::array<NonBroadphaseShape,max_handles> non_broadphase{}; // Element ID indexed.
    std::uintptr_t native_scene{};
    struct RigidOwner {
        std::uintptr_t actor{},simulation{};
        friend bool operator==(const RigidOwner&,const RigidOwner&)=default;
    };
    std::array<RigidOwner,max_handles> rigid_members{}; // Native rigid ID indexed.
    std::uintptr_t anchor_owner{},anchor_core{};
    unsigned anchor_id{};
    unsigned aggregate_free_head{};
    std::array<unsigned,max_handles> aggregate_free_links{};
    unsigned aggregate_slots{},aggregate_count{};
    std::array<EmptyAggregate,8> empty_aggregates{};
    static const char* AdmissionName(unsigned step) noexcept {
        constexpr const char* names[]{"sap_complete","sap_binding","sap_pending_scratch_or_aggregate","sap_extent",
            "sap_scratch_order","sap_bound_owners","sap_shape_inputs","sap_endpoints_and_pending_handles",
            "sap_pair_storage","sap_pair_membership","sap_semantic_image",
            "sap_scratch_150","sap_scratch_1a0","sap_scratch_1b0","sap_scratch_200","sap_scratch_248","sap_scratch_290",
            "sap_deleted_pairs_pending","sap_aggregate_ownership","sap_dirty_aggregates","sap_aggregate_pairs",
            "sap_actor_aggregate_pairs","sap_out_of_bounds_objects","sap_out_of_bounds_aggregates","sap_id_continuation",
            "sap_shape_id_coverage","sap_rigid_id_coverage","sap_element_id_coverage","sap_static_anchor"};
        return step<std::size(names)?names[step]:"sap_unobserved";
    }

    static std::array<bool,max_handles> ActiveIds(const IdContinuation& id) noexcept {
        std::array<bool,max_handles> active{};
        for(unsigned i=0;i<id.next && i<max_handles;++i)active[i]=true;
        for(unsigned i=0;i<id.count && i<max_handles;++i)if(id.free[i]<max_handles)active[id.free[i]]=false;
        return active;
    }
    unsigned IdCoverageFailure() const noexcept {
        std::array<bool,max_handles> shapes{},rigids{},elements{};
        std::array<std::uintptr_t,max_handles> rigid_owners{};
        if(!anchor_owner || !anchor_core || anchor_id>=max_handles)return 28;
        rigids[anchor_id]=true;rigid_owners[anchor_id]=anchor_owner;
        for(unsigned i=0;i<max_handles;++i)if(rigid_members[i].simulation) {
            if(!rigid_members[i].actor || rigids[i])return 26;
            rigids[i]=true;rigid_owners[i]=rigid_members[i].simulation;
        }
        for(unsigned i=0;i<extent;++i) {
            const auto& v=volumes[i];if(!v.shape)continue;
            elements[i]=true;if(v.empty_aggregate)continue;
            if(v.shape_id>=max_handles || shapes[v.shape_id])return 25;
            shapes[v.shape_id]=true;
            if(v.rigid_id>=max_handles || (rigid_owners[v.rigid_id] && rigid_owners[v.rigid_id]!=v.actor))return 26;
            rigids[v.rigid_id]=true;rigid_owners[v.rigid_id]=v.actor;
        }
        for(unsigned i=0;i<max_handles;++i) {
            const auto& v=non_broadphase[i];if(!v.shape)continue;
            if(!v.core || !v.actor || (v.flags&5) || elements[i])return 27;
            elements[i]=true;
            if(v.shape_id>=max_handles || shapes[v.shape_id])return 25;
            shapes[v.shape_id]=true;
            if(v.rigid_id>=max_handles || (rigid_owners[v.rigid_id] && rigid_owners[v.rigid_id]!=v.actor))return 26;
            rigids[v.rigid_id]=true;rigid_owners[v.rigid_id]=v.actor;
        }
        if(shapes!=ActiveIds(ids[1]))return 25;
        if(rigids!=ActiveIds(ids[2]))return 26;
        if(elements!=ActiveIds(ids[3]))return 27;
        return 0;
    }
    bool WellFormed() const noexcept {
        if(!extent || extent>max_handles || boxes>extent || previous_boxes!=boxes
            || pair_count>max_pairs || bounds_changed>1 || origin_shifted || persistent_changed>1)return false;
        for(const auto& id:ids) {
            if(!id.owner || id.next>max_handles || id.count>id.next)return false;
            std::array<bool,max_handles> free{};
            for(unsigned i=0;i<id.count;++i){if(id.free[i]>=id.next || free[id.free[i]])return false;free[id.free[i]]=true;}
        }
        if(IdCoverageFailure())return false;
        for(unsigned axis=0;axis<3;++axis) {
            if(values[axis][0]!=0 || values[axis][boxes*2+1]!=0xffffffffu
                || owners[axis][0]!=invalid-1 || owners[axis][boxes*2+1]!=invalid)return false;
            std::array<unsigned char,max_handles> seen{};
            for(unsigned i=1;i<=boxes*2;++i) {
                const auto id=owners[axis][i]>>1,side=owners[axis][i]&1;
                if(id>=extent || !volumes[id].shape || (seen[id]&(1u<<side))
                    || box_endpoints[axis][id][side]!=i || values[axis][i]<values[axis][i-1])return false;
                seen[id]|=1u<<side;
            }
            for(unsigned i=0;i<extent;++i) {
                if((volumes[i].empty_aggregate || !volumes[i].shape)
                    && (seen[i] || (pending[axis][i/32]&(1u<<(i%32)))))return false;
                if(seen[i] && seen[i]!=3)return false;
                if(!seen[i] && (box_endpoints[axis][i][0]!=box_endpoints[axis][i][1]
                    || (box_endpoints[axis][i][0]!=invalid && box_endpoints[axis][i][0]!=invalid-2)))return false;
            }
        }
        for(unsigned i=0;i<pair_count;++i) {
            const auto& p=pairs[i];
            if(p.handles[0]>=p.handles[1] || p.handles[1]>=extent || p.state
                || !volumes[p.handles[0]].shape || !volumes[p.handles[1]].shape)return false;
            for(unsigned axis=0;axis<3;++axis) {
                const auto& a=box_endpoints[axis][p.handles[0]];const auto& b=box_endpoints[axis][p.handles[1]];
                if(a[0]>=b[1] || b[0]>=a[1])return false;
            }
            for(unsigned j=0;j<i;++j)if(p.handles==pairs[j].handles)return false;
        }
        return true;
    }
    bool SameOwners(const ReplayPhysicsSapImage& b,const char** check=nullptr,unsigned* row=nullptr,
        std::uint64_t* expected=nullptr,std::uint64_t* observed=nullptr) const noexcept {
        const auto equal=[&](auto a,auto v,const char* field,unsigned index=0xffffffffu) {
            if(a==v)return true;
            if(check)*check=field;if(row)*row=index;
            if(expected)*expected=static_cast<std::uint64_t>(a);if(observed)*observed=static_cast<std::uint64_t>(v);
            return false;
        };
        if(!valid || !b.valid)return equal(valid,b.valid,"sap_image_valid") && false;
        if(!equal(scene,b.scene,"sap_scene_owner") || !equal(manager,b.manager,"sap_manager_owner")
            || !equal(broadphase,b.broadphase,"sap_broadphase_owner") || !equal(bounds_owner,b.bounds_owner,"sap_bounds_owner")
            || !equal(distance_owner,b.distance_owner,"sap_distance_owner")
            || !equal(anchor_owner,b.anchor_owner,"sap_anchor_owner") || !equal(anchor_core,b.anchor_core,"sap_anchor_core")
            || !equal(anchor_id,b.anchor_id,"sap_anchor_id")
            || !equal(native_scene,b.native_scene,"sap_native_scene")
            || !equal(aggregate_slots,b.aggregate_slots,"sap_aggregate_slots") || !equal(aggregate_count,b.aggregate_count,"sap_aggregate_count"))return false;
        for(unsigned i=0;i<ids.size();++i) {
            const auto& a=ids[i];const auto& v=b.ids[i];
            if(!equal(a.owner,v.owner,"sap_id_owner",i))return false;
            if(i==0) {
                // Constraint allocation is outside this shape/rigid transaction.
                if(!equal(a.next,v.next,"sap_constraint_id_next",i) || !equal(a.count,v.count,"sap_constraint_id_free_count",i))return false;
                for(unsigned j=0;j<a.count;++j)if(!equal(a.free[j],v.free[j],"sap_constraint_id_free_order",j))return false;
            } else {
                const auto left=ActiveIds(a),right=ActiveIds(v);
                for(unsigned j=0;j<max_handles;++j)if(!equal(left[j],right[j],"sap_active_id_changed",i*max_handles+j))return false;
            }
        }
        for(unsigned i=0;i<max_handles;++i)
            if(non_broadphase[i]!=b.non_broadphase[i])return equal(false,true,"sap_non_broadphase_owner",i);
        for(unsigned i=0;i<max_handles;++i)
            if(rigid_members[i]!=b.rigid_members[i])return equal(false,true,"sap_rigid_membership",i);
        if(!equal(aggregate_free_head,b.aggregate_free_head,"sap_aggregate_free_head"))return false;
        for(unsigned i=0;i<aggregate_slots;++i)
            if(!equal(aggregate_free_links[i],b.aggregate_free_links[i],"sap_aggregate_free_order",i))return false;
        for(unsigned i=0;i<empty_aggregates.size();++i) {
            const auto& a=empty_aggregates[i];const auto& v=b.empty_aggregates[i];
            if(!equal(a.owner,v.owner,"sap_aggregate_owner",i) || !equal(a.self,v.self,"sap_aggregate_self",i)
                || !equal(a.slot,v.slot,"sap_aggregate_slot",i) || !equal(a.handle,v.handle,"sap_aggregate_handle",i))return false;
        }
        for(unsigned i=0;i<(extent>b.extent?extent:b.extent);++i) {
            if(i>=extent || i>=b.extent) {
                const auto& tail=i>=extent?b:*this;
                if(!equal(std::uintptr_t{},tail.volumes[i].shape,"sap_active_handle_tail",i))return false;
                continue; // WellFormed proves no endpoint, pair or pending input uses this retired slot.
            }
            const auto& a=volumes[i];const auto& v=b.volumes[i];
            if(!equal(a.shape,v.shape,"sap_shape_owner",i) || !equal(a.core,v.core,"sap_shape_core",i)
                || !equal(a.actor,v.actor,"sap_actor_owner",i) || !equal(a.group,v.group,"sap_filter_group",i)
                || !equal(a.contact_distance,v.contact_distance,"sap_contact_distance",i)
                || !equal(a.empty_aggregate,v.empty_aggregate,"sap_volume_kind",i)
                || !equal(a.attributes,v.attributes,"sap_filter_attributes",i)
                || !equal(a.shape_id,v.shape_id,"sap_shape_id_owner",i) || !equal(a.rigid_id,v.rigid_id,"sap_rigid_id_owner",i))return false;
            for(unsigned j=0;j<4;++j)if(!equal(a.filter_data[j],v.filter_data[j],
                j==0?"sap_filter_word0":j==1?"sap_filter_word1":j==2?"sap_filter_word2":"sap_filter_word3",i))return false;
        }
        return true;
    }
    bool SameContinuation(const ReplayPhysicsSapImage& b) const noexcept {
        if(!WellFormed() || !b.WellFormed() || !SameOwners(b) || boxes!=b.boxes || previous_boxes!=b.previous_boxes
            || pair_count!=b.pair_count || ids!=b.ids || pending!=b.pending || bounds_changed!=b.bounds_changed
            || origin_shifted!=b.origin_shifted || persistent_changed!=b.persistent_changed)return false;
        for(unsigned axis=0;axis<3;++axis)for(unsigned i=0;i<boxes*2+2;++i)
            if(values[axis][i]!=b.values[axis][i] || owners[axis][i]!=b.owners[axis][i])return false;
        const auto common=extent<b.extent?extent:b.extent;
        for(unsigned i=0;i<common;++i) {
            if(volumes[i]!=b.volumes[i])return false;
            for(unsigned axis=0;axis<3;++axis)if(box_endpoints[axis][i]!=b.box_endpoints[axis][i])return false;
        }
        for(unsigned i=0;i<pair_count;++i)if(pairs[i]!=b.pairs[i])return false;
        return true;
    }
    friend bool operator==(const ReplayPhysicsSapImage&,const ReplayPhysicsSapImage&)=default;
};
}
