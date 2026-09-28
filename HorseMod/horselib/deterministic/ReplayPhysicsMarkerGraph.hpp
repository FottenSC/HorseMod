#pragma once
#include "Sc6ReplayWorldState.hpp"
#include <array>
#include <cstring>

namespace Horse::Deterministic {
// A semantic registration graph. Pool addresses are bindings, never replay
// input. Only the verified early SUPPRESS paths in SC6 14204CF60 are admitted.
struct ReplayPhysicsMarkerGraph {
    using Image=Sc6ReplayWorldState::PhysicsBoundary;
    using Marker=Image::ActorObservation::InteractionObservation;
    static constexpr unsigned capacity=128;
    struct Entry {
        Marker marker{};
        std::array<std::uintptr_t,2> owners{};
        std::array<unsigned,3> indices{};
        friend bool operator==(const Entry&,const Entry&)=default;
    };
    std::array<Entry,capacity> entries{};
    unsigned count{};
    struct ReadFailure {
        const char* check{"marker_graph_complete"};
        unsigned actor{0xffffffffu},interaction{0xffffffffu};
        std::uint64_t expected{},observed{};
    };

    static bool Suppressed(const Marker& m) noexcept {
        if(!m.marker_filter_valid)return false;
        const auto a=m.marker_attributes[0],b=m.marker_attributes[1];
        if((a!=0 && a!=0x11) || (b!=0 && b!=0x11))return false;
        if((a==0x11 && b==0) || (a==0 && b==0x11))return true;
        return a==0x11 && b==0x11
            && ((m.marker_filter_data[0][3]>>21)&31)!=7
            && ((m.marker_filter_data[1][3]>>21)&31)!=7;
    }
    static bool InputsEqual(const Marker& a,const Marker& b) noexcept {
        return a.marker_elements==b.marker_elements && a.marker_shape_cores==b.marker_shape_cores
            && a.marker_attributes==b.marker_attributes && a.marker_filter_data==b.marker_filter_data
            && a.marker_filter_valid && b.marker_filter_valid;
    }
    static bool DecodeRegistration(const Marker& m,Entry& out,bool shared_map=false) noexcept {
        if(!m.address || !m.marker_registered || !m.marker_filter_valid
            || m.header[0x24]!=std::byte{2} || m.marker_filter_pair!=0xffffffffu
            || m.marker_scene_active || m.marker_scene_count>capacity
            || (shared_map?(m.marker_map_count<m.marker_scene_count || m.marker_map_count>capacity*2)
                :m.marker_map_count!=m.marker_scene_count))return false;
        out.marker=m;
        std::memcpy(out.owners.data(),m.header.data()+8,16);
        std::memcpy(out.indices.data(),m.header.data()+0x18,12);
        if(!out.owners[0] || !out.owners[1] || out.owners[0]==out.owners[1]
            || !m.marker_elements[0] || !m.marker_elements[1] || m.marker_elements[0]==m.marker_elements[1]
            || !m.marker_shape_cores[0] || !m.marker_shape_cores[1]
            || out.indices[0]>=m.marker_scene_count)return false;
        for(unsigned side=0;side<2;++side)
            if(m.marker_actor_counts[side]>capacity || out.indices[side+1]>=m.marker_actor_counts[side])return false;
        return true;
    }
    static bool Decode(const Marker& m,Entry& out) noexcept {
        return Suppressed(m) && m.header[0x25]==std::byte{0xb}
            && m.header[0x26]==std::byte{} && DecodeRegistration(m,out);
    }
    // Only commit reads C without reconstructing it. A queued refilter can
    // legitimately retain a marker whose new inputs no longer suppress it.
    // Caller must prove exact native dirty-set membership before retirement.
    static bool DecodeCommit(const Marker& m,Entry& out) noexcept {
        if(m.header[0x25]==std::byte{0xb})return Suppressed(m) && m.header[0x26]==std::byte{} && DecodeRegistration(m,out,true);
        const auto dirty=std::to_integer<unsigned>(m.header[0x26]);
        return m.header[0x25]==std::byte{0x1b} && dirty && !(dirty&~0x3fu)
            && ((dirty&1) || Suppressed(m)) && DecodeRegistration(m,out,true);
    }
    bool Complete(bool partial_actors=false) const noexcept {
        if(count>capacity)return false;
        for(unsigned i=0;i<count;++i) {
            const auto& e=entries[i];
            if(e.indices[0]!=i || e.marker.marker_scene_count!=count)return false;
            for(unsigned j=0;j<i;++j) {
                const auto& prior=entries[j];
                if(prior.marker.address==e.marker.address
                    || prior.marker.marker_elements==e.marker.marker_elements
                    || (prior.marker.marker_elements[0]==e.marker.marker_elements[1]
                        && prior.marker.marker_elements[1]==e.marker.marker_elements[0]))return false;
            }
            for(unsigned side=0;side<2;++side) {
                std::array<bool,capacity> indices{};unsigned members{};
                for(unsigned j=0;j<count;++j)for(unsigned other=0;other<2;++other)
                    if(entries[j].owners[other]==e.owners[side]) {
                        const auto index=entries[j].indices[other+1];
                        if(index>=e.marker.marker_actor_counts[side] || indices[index]
                            || entries[j].marker.marker_actor_counts[other]!=e.marker.marker_actor_counts[side])return false;
                        indices[index]=true;++members;
                    }
                if(!partial_actors && members!=e.marker.marker_actor_counts[side])return false;
            }
        }
        return true;
    }
    bool Read(const Image& image,unsigned scene,ReadFailure* failure=nullptr) noexcept {
        count=0;
        const auto reject=[&](const char* check,unsigned actor,unsigned interaction,
            std::uint64_t expected=0,std::uint64_t observed=0) {
            if(failure)*failure={check,actor,interaction,expected,observed};
            return false;
        };
        if(!image.valid_counts() || scene>=image.scenes.size())
            return reject("marker_graph_image_counts",0xffffffffu,0xffffffffu);
        std::array<bool,capacity> seen{};
        for(unsigned actor=0;actor<image.observed_actors[scene];++actor) {
            const auto& row=image.actors[scene][actor];
            for(unsigned i=0;i<row.interaction_count;++i) {
                const auto& marker=row.interactions[i];
                if(marker.header[0x24]!=std::byte{2})continue;
                Entry e{};if(!Decode(marker,e)) {
                    if(!marker.address || !marker.marker_registered)
                        return reject("marker_graph_registration",actor,i,1,marker.marker_registered);
                    if(!Suppressed(marker))
                        return reject("marker_graph_suppression",actor,i,marker.marker_attributes[0],marker.marker_attributes[1]);
                    if(marker.marker_map_count!=marker.marker_scene_count)
                        return reject("marker_graph_map_count",actor,i,marker.marker_scene_count,marker.marker_map_count);
                    if(marker.marker_scene_active)
                        return reject("marker_graph_active_scene",actor,i,0,marker.marker_scene_active);
                    if(marker.marker_filter_pair!=0xffffffffu)
                        return reject("marker_graph_filter_pair",actor,i,0xffffffffu,marker.marker_filter_pair);
                    return reject("marker_graph_decode",actor,i,0xb,
                        std::to_integer<unsigned>(marker.header[0x25]) | (std::to_integer<unsigned>(marker.header[0x26])<<8));
                }
                const auto side=row.simulation==e.owners[0]?0u:row.simulation==e.owners[1]?1u:2u;
                if(side==2)return reject("marker_graph_actor_owner",actor,i,row.simulation,e.owners[0]);
                if(e.indices[side+1]!=i)return reject("marker_graph_actor_index",actor,i,i,e.indices[side+1]);
                if(row.interaction_count!=marker.marker_actor_counts[side])
                    return reject("marker_graph_actor_count",actor,i,row.interaction_count,marker.marker_actor_counts[side]);
                if(count && count!=marker.marker_scene_count)
                    return reject("marker_graph_scene_count",actor,i,count,marker.marker_scene_count);
                count=marker.marker_scene_count;
                if(seen[e.indices[0]] && entries[e.indices[0]]!=e)
                    return reject("marker_graph_duplicate_observation",actor,i,e.indices[0],e.indices[0]);
                entries[e.indices[0]]=e;seen[e.indices[0]]=true;
            }
        }
        for(unsigned i=0;i<count;++i)if(!seen[i])return reject("marker_graph_missing_scene_row",0xffffffffu,i,count,i);
        if(!Complete())return reject("marker_graph_incomplete_membership",0xffffffffu,0xffffffffu,count,count);
        if(failure)*failure={};
        return true;
    }
    bool TargetRetainedIn(const ReplayPhysicsMarkerGraph& current) const noexcept {
        for(unsigned i=0;i<count;++i) {
            bool found{};
            for(unsigned j=0;j<current.count;++j)
                if(InputsEqual(entries[i].marker,current.entries[j].marker)
                    && entries[i].owners==current.entries[j].owners)found=true;
            if(!found)return false;
        }
        return true;
    }
};
}
