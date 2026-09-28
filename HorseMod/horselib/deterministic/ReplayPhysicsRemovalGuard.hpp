#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Horse::Deterministic {
// Read-only ownership guard for selective native body retirement. This does
// not classify a body as cosmetic, retain its UObject owner, or authorize SAP
// postprocessing with unrelated aggregate/scratch work pending. Those remain
// enclosing transaction preconditions. No captured bytes are installed.
class ReplayPhysicsRemovalGuard final {
public:
    ReplayPhysicsRemovalGuard()=default;
    ReplayPhysicsRemovalGuard(const ReplayPhysicsRemovalGuard&)=delete;
    ReplayPhysicsRemovalGuard& operator=(const ReplayPhysicsRemovalGuard&)=delete;
    static constexpr unsigned max_actors=64,max_bodies=16,max_handles=8192;
    struct alignas(16) Packet {
        const unsigned* created{};unsigned created_count{},reserved0{};
        const unsigned* updated{};unsigned updated_count{},reserved1{};
        const unsigned* removed{};unsigned removed_count{},reserved2{};
        std::uintptr_t bounds{},groups{},distances{};
        unsigned capacity{};std::uint8_t bounds_changed{};std::array<std::byte,3> reserved3{};
    };
    static_assert(sizeof(Packet)==0x50 && offsetof(Packet,removed)==0x20
        && offsetof(Packet,bounds)==0x30 && offsetof(Packet,capacity)==0x48);
    enum class Check {Input,Scene,Actors,BodyBinding,Volumes,Shape,Created,Removed,Updated,Ready,Aggregates,Ids,Nodes};
    Check failed_check() const noexcept {return check_;}
    // Current collision admission needs the aggregate proof without acquiring
    // a removal operation or assuming that selected bodies occupy the tail.
    template<class Read>
    bool ValidateCollisionAggregates(Read read,std::uintptr_t module,std::uintptr_t scene) noexcept {
        if(state_!=State::Empty)return false;
        module_=module;scene_=scene;
        std::uintptr_t type{};unsigned phase{};
        check_=Check::Scene;
        if(!read(scene,type) || type!=module+0x19d008 || !read(scene+0x1f14,phase) || phase
            || !read(scene+0x738,aabb_) || !aabb_ || !read(aabb_+0x1c0,extent_)
            || extent_>max_handles || !read(aabb_+0xa8,volumes_) || !volumes_
            || !read(aabb_+0x100,sap_) || !sap_ || !read(sap_,type) || type!=module+0x1ace30)return false;
        check_=Check::Aggregates;
        if(!CaptureAggregates(read))return false;
        for(unsigned i=0;i<extent_;++i) {
            std::uintptr_t user{};unsigned tag{};
            if(!read(volumes_+i*16,user))return false;
            if(!user)continue;
            if(!read(volumes_+i*16+8,tag))return false;
            if(tag!=0xffffffffu && !EmptyAggregateVolume(read,i,user,tag))return false;
        }
        for(unsigned i=0;i<aggregate_count_;++i) {
            const auto& row=aggregates_[i];std::uintptr_t user{};unsigned tag{};
            if(!read(volumes_+row.handle*16,user) || !read(volumes_+row.handle*16+8,tag)
                || tag!=row.slot*2+1 || !EmptyAggregateVolume(read,row.handle,user,tag))return false;
        }
        check_=Check::Ready;return true;
    }
    template<class Read>
    bool Capture(Read read,std::uintptr_t module,std::uintptr_t scene,
                 std::span<const std::uintptr_t> bodies) noexcept {
        if(state_!=State::Empty)return false;
        check_=Check::Input;
        if(bodies.empty() || bodies.size()>max_bodies)return false;
        module_=module;scene_=scene;count_=0;body_count_=static_cast<unsigned>(bodies.size());removed_={};created_={};
        id_owners_={};deleted_ids_={};deleted_nodes_=0;
        std::uintptr_t table{},vtable{};unsigned capacity{},phase{},extent{};
        check_=Check::Scene;
        if(!read(scene,vtable) || vtable!=module+0x19d008
            || !read(scene+0x1f14,phase) || phase
            || !read(scene+0x2590,table) || !read(scene+0x2598,count_)
            || !read(scene+0x259c,capacity) || count_>max_actors || count_<body_count_
            || count_>(capacity&0x7fffffffu) || !table
            || !read(scene+0x738,aabb_) || !aabb_ || !read(aabb_+0x1c0,extent)
            || extent>max_handles || !read(aabb_+0xa8,volumes_) || !volumes_)return false;
        if(!read(aabb_+0x100,sap_) || !sap_ || !read(sap_,vtable)
            || vtable!=module+0x1ace30)return false;
        extent_=extent;
        check_=Check::Nodes;
        if(!read(scene_+0x758,islands_) || !islands_)return false;
        check_=Check::Ids;
        for(unsigned k=0;k<id_owners_.size();++k) {
            unsigned pending{},words{};std::uintptr_t bits{};
            if(!read(scene_+0x1128+k*8,id_owners_[k]) || !id_owners_[k]
                || !read(id_owners_[k]+0x30,pending) || pending
                || !read(id_owners_[k]+0x18,bits) || !read(id_owners_[k]+0x20,words))return false;
            words&=0x7fffffffu;
            if(words>max_handles/32 || (words&&!bits))return false;
            for(unsigned j=0;j<words;++j) {unsigned value{};if(!read(bits+j*4,value) || value)return false;}
        }
        check_=Check::Aggregates;
        if(!CaptureAggregates(read))return false;
        for(unsigned i=0;i<count_;++i) {
            check_=Check::Actors;
            auto& row=actors_[i];
            if(!read(table+i*8,row.actor) || !row.actor || !read(row.actor,row.type)
                || (row.type!=module+0x19b7c0 && row.type!=module+0x19be60)
                || !read(row.actor+0x80,row.sim))return false;
            for(unsigned previous=0;previous<i;++previous)
                if(actors_[previous].actor==row.actor)return false;
            if(i>=count_-body_count_) {
                check_=Check::BodyBinding;
                if(row.actor!=bodies[i-(count_-body_count_)] || row.type!=module+0x19b7c0 || !row.sim)return false;
                std::uintptr_t owner{},core{};
                if(!read(row.sim+0x40,owner) || owner!=scene+0x10
                    || !read(row.sim+0x48,core) || core!=row.actor+0x80)return false;
                unsigned rigid_id{};
                if(!read(row.sim+0x50,rigid_id) || rigid_id>=max_handles
                    || (deleted_ids_[2][rigid_id/32]&(1u<<(rigid_id%32))))return false;
                deleted_ids_[2][rigid_id/32]|=1u<<(rigid_id%32);
                unsigned node{};
                if(!read(row.sim+0xb0,node) || (node&63) || (node>>6)>=64
                    || (deleted_nodes_&(std::uint64_t{1}<<(node>>6))))return false;
                deleted_nodes_|=std::uint64_t{1}<<(node>>6);
                // Native133D00 allocates an element ID and links every shape
                // at ActorSim+38 via113F30. Native115940 can omit broadphase
                // admission for flags&5==0, but native destruction still frees
                // its element and shape IDs. Inventory that complete list;
                // the SAP removal packet below remains broadphase-only.
                std::uintptr_t element{};unsigned elements{};
                if(!read(row.sim+0x38,element))return false;
                while(element) {
                    check_=Check::Shape;
                    std::uintptr_t type{},owner{},core{},volume{};unsigned id{},shape_id{};unsigned char flags{};
                    if(++elements>max_handles || !read(element,type) || type!=module+0x1aae40
                        || !read(element+0x10,owner) || owner!=row.sim
                        || !read(element+0x18,id) || !read(element+0x48,shape_id)
                        || !read(element+0x40,core) || !core || !read(core+0x40,flags))return false;
                    id&=0x1fffffffu;
                    if(id>=max_handles || shape_id>=max_handles
                        || (deleted_ids_[3][id/32]&(1u<<(id%32)))
                        || (deleted_ids_[1][shape_id/32]&(1u<<(shape_id%32))))return false;
                    if(id<extent_ && !read(volumes_+id*16,volume))return false;
                    if(volume?volume!=element:bool(flags&5))return false;
                    deleted_ids_[3][id/32]|=1u<<(id%32);
                    deleted_ids_[1][shape_id/32]|=1u<<(shape_id%32);
                    if(!read(element+8,element))return false;
                }
            }
        }
        // Remove in reverse tail order. Native removeActor swaps the last
        // actor into a removed slot; this admission leaves every other index
        // and owner untouched. Non-tail removal needs a separate order undo.
        for(unsigned i=0;i<extent_;++i) {
            check_=Check::Volumes;
            std::uintptr_t shape{},sim{};
            if(!read(volumes_+i*16,shape))return false;
            if(!shape)continue;
            unsigned aggregate{};
            if(!read(volumes_+i*16+8,aggregate))return false;
            // Empty aggregate roots have handles but no endpoints or member
            // work. Reuse the native ownership conditions from ReadPhysicsSap;
            // never skip an unaccounted/nonempty aggregate removal domain.
            if(aggregate!=0xffffffffu) {
                if(!EmptyAggregateVolume(read,i,shape,aggregate))return false;
                continue;
            }
            if(!read(shape+0x10,sim))return false;
            for(unsigned b=count_-body_count_;b<count_;++b)if(sim==actors_[b].sim) {
                check_=Check::Shape;
                std::uintptr_t type{};unsigned id{};
                if(!read(shape,type) || type!=module+0x1aae40 || !read(shape+0x18,id)
                    || (id&0x1fffffffu)!=i)return false;
                removed_[i/32]|=1u<<(i%32);
                unsigned shape_id{};
                if(!read(shape+0x48,shape_id) || shape_id>=max_handles
                    || !(deleted_ids_[3][i/32]&(1u<<(i%32)))
                    || !(deleted_ids_[1][shape_id/32]&(1u<<(shape_id%32))))return false;
            }
        }
        check_=Check::Nodes;if(!NodeWorkIdle(read) && !OwnedActivationInputs(read))return false;
        check_=Check::Created;
        std::uintptr_t created_bits{};unsigned created_words{};
        if(!read(aabb_+0x48,created_bits) || !read(aabb_+0x50,created_words))return false;
        created_words&=0x7fffffffu;
        if(created_words>created_.size() || (created_words&&!created_bits))return false;
        for(unsigned i=0;i<created_words;++i)
            if(!read(created_bits+i*4,created_[i]) || (created_[i]&~removed_[i]))return false;
        check_=Check::Removed;if(!Bits(read,0x58,false))return false;
        check_=Check::Updated;
        std::uintptr_t pending{};unsigned words{};
        if(!read(aabb_+0x68,pending) || !read(aabb_+0x70,words))return false;
        words&=0x7fffffffu;updated_={};
        if(words>updated_.size() || (words&&!pending))return false;
        for(unsigned i=0;i<words;++i)if(!read(pending+i*4,updated_[i]))return false;
        state_=State::Captured;check_=Check::Ready;return true;
    }
    template<class Read>
    bool Detached(Read read) const noexcept {
        return state_!=State::Empty && Members(read) && Bits(read,0x48,false)
            && Bits(read,0x58,true) && PendingUpdatesPreserved(read);
    }
    // 14A890 marks both endpoints dirty when a native edge is removed.
    // 1389D0/138A70 deliberately skip the dirty-node traversal. Finish only
    // work derived from our removal: the original boundary had no pending
    // dirty/input queues, current receipts must be empty, and every surviving
    // dirty endpoint must be a retained kinematic. No simulation step occurs.
    template<class Read> bool PrepareNodeRetirement(Read read,unsigned& mask) const noexcept {
        mask=0;
        if(!Detached(read))return false;
        std::uintptr_t owner{};unsigned pending{};
        if(!read(scene_+0x758,owner) || owner!=islands_ || !read(owner+0x38,pending) || pending)return false;
        for(unsigned slot=0;slot<2;++slot) {
            const auto island=owner+(slot?0x310u:0xb0u);
            std::uintptr_t nodes{},bits{};unsigned count{},words{};
            if(!ReadNodeInputs(read,island,nodes,count,bits,words))return false;
            for(unsigned j=0;j<words;++j) {
                unsigned value{};if(!read(bits+j*4,value))return false;
                for(unsigned bit=0;bit<32;++bit)if(value&(1u<<bit)) {
                    const auto id=j*32+bit;
                    if(id>=count || id>=64)return false;
                    const auto node=nodes+id*0x20;unsigned char flags{};std::uintptr_t binding{};
                    if(!read(node+4,flags) || !read(node+0x18,binding))return false;
                    if(flags==8 && !binding) {
                        if(!(deleted_nodes_&(std::uint64_t{1}<<id)))return false;
                    } else {
                        if(!(flags&4) || !(flags&0x10) || (flags&0x68))return false;
                        bool found{};
                        for(unsigned i=0;i<count_-body_count_;++i)if(actors_[i].sim && binding==actors_[i].sim+0x60) {
                            unsigned handle{};unsigned char body_flags{};
                            if(found || actors_[i].type!=module_+0x19b7c0
                                || !read(actors_[i].sim+0xb0,handle) || handle!=id*64
                                || !read(actors_[i].actor+0xac,body_flags) || !(body_flags&1))return false;
                            found=true;
                        }
                        if(!found)return false;
                    }
                    mask|=1u<<slot;
                }
            }
            // With deactivation disabled, do not expose a ready-to-sleep
            // dynamic island to the remaining native graph finalization.
            for(unsigned id=0;id<count;++id) {
                unsigned char flags{};
                if(!read(nodes+id*0x20+4,flags))return false;
                if(!(flags&12) && (flags&0x21))return false;
            }
        }
        return true;
    }
    template<class Read> bool NodeWorkIdle(Read read) const noexcept {
        unsigned pending{};
        if(!islands_ || !read(islands_+0x38,pending) || pending)return false;
        for(unsigned slot=0;slot<2;++slot) {
            std::uintptr_t nodes{},bits{};unsigned count{},words{};
            if(!ReadNodeInputs(read,islands_+(slot?0x310u:0xb0u),nodes,count,bits,words))return false;
            for(unsigned j=0;j<words;++j) {unsigned value{};if(!read(bits+j*4,value) || value)return false;}
            for(unsigned id=0;id<count;++id) {unsigned char flags{};if(!read(nodes+id*0x20+4,flags) || (flags&0x10))return false;}
        }
        return true;
    }
    template<class Read>
    bool Retired(Read read) const noexcept {
        return state_!=State::Empty && Members(read) && Bits(read,0x48,false)
            && Bits(read,0x58,false) && PendingUpdatesPreserved(read);
    }
    // Native FF080 records both delayed free IDs and deletion bits. E75D0
    // finishes shape/rigid/constraint IDs; F1ED0 separately finishes elements.
    // Admit that tail only when every pending ID belongs to this exact removal.
    template<class Read> bool PrepareIdRetirement(Read read,std::uintptr_t& bitmap,std::size_t& bytes) const noexcept {
        bitmap=0;bytes=0;
        if(state_!=State::PacketPrepared)return false;
        for(unsigned k=0;k<id_owners_.size();++k) {
            std::uintptr_t owner{},bits{},list{};unsigned words{},count{},capacity{};
            if(!read(scene_+0x1128+k*8,owner) || owner!=id_owners_[k]
                || !read(owner+0x18,bits) || !read(owner+0x20,words)
                || !read(owner+0x28,list) || !read(owner+0x30,count) || !read(owner+0x34,capacity))return false;
            words&=0x7fffffffu;capacity&=0x7fffffffu;
            if(words>max_handles/32 || (words&&!bits) || count>max_handles || count>capacity || (count&&!list))return false;
            for(unsigned j=0;j<max_handles/32;++j) {
                unsigned value{};if(j<words && !read(bits+j*4,value))return false;
                if(value!=deleted_ids_[k][j])return false;
            }
            std::array<unsigned,max_handles/32> seen{};
            for(unsigned j=0;j<count;++j) {
                unsigned id{};if(!read(list+j*4,id) || id>=max_handles || (seen[id/32]&(1u<<(id%32))))return false;
                seen[id/32]|=1u<<(id%32);
            }
            if(seen!=deleted_ids_[k])return false;
            if(k==3) {bitmap=bits;bytes=std::size_t(words)*4;}
        }
        return true;
    }
    // Read only at an independently joined native boundary with the original
    // scene retained. Task completion alone leaves SAP+90 borrowing indices_.
    // This is storage detachment evidence, not task/GPU completion or a scene
    // lifetime lease. Never use a zero list count to waive a dangling pointer.
    template<class Read> bool InputStorageDetached(Read read) const noexcept {
        if(state_!=State::PacketPrepared)return false;
        std::uintptr_t type{},aabb{},sap{};unsigned phase{};
        if(!read(scene_,type) || type!=module_+0x19d008
            || !read(scene_+0x1f14,phase) || phase
            || !read(scene_+0x738,aabb) || aabb!=aabb_
            || !read(aabb+0x100,sap) || sap!=sap_
            || !read(sap,type) || type!=module_+0x1ace30)return false;
        const auto begin=reinterpret_cast<std::uintptr_t>(indices_.data());
        const auto end=begin+sizeof(indices_);
        for(const unsigned offset:{0x80u,0x90u,0xa0u}) {
            std::uintptr_t input{};
            if(!read(sap+offset,input) || (input>=begin && input<end))return false;
        }
        return true;
    }
    // Native1570E0 has an allocation-free empty-input path. After our joined
    // removal, its update bitmap is already zero. Rebinding only these spent
    // inputs avoids waiting for a gameplay tick during prepublication cancel.
    // Do not call on a different native update, live scratch, or a bitmap that
    // would be changed. Pending AABB created/updated work is not consumed.
    template<class Read> bool PrepareInputRetirement(Read read,Packet& output) const noexcept {
        if(state_!=State::PacketPrepared)return false;
        std::uintptr_t type{},aabb{},sap{},pointer{},bitmap{};unsigned phase{},value{},boxes{},endpoints{};
        if(!read(scene_,type) || type!=module_+0x19d008 || !read(scene_+0x1f14,phase) || phase
            || !read(scene_+0x738,aabb) || aabb!=aabb_ || !read(aabb+0x100,sap) || sap!=sap_
            || !read(sap,type) || type!=module_+0x1ace30
            || !read(sap+0x80,pointer) || pointer || !read(sap+0x88,value) || value
            || !read(sap+0xa0,pointer) || pointer || !read(sap+0xa8,value) || value
            || !read(sap+0x90,pointer) || pointer!=reinterpret_cast<std::uintptr_t>(indices_.data())
            || !read(sap+0x98,value) || value!=packet_.removed_count)return false;
        for(unsigned offset:{0x150u,0x1a0u,0x1b0u,0x200u,0x248u,0x290u})
            if(!read(sap+offset,pointer) || pointer || !read(sap+offset+8,pointer) || pointer)return false;
        Packet empty{};
        if(!read(sap+0x1c0,value) || value || !read(sap+0xc8,empty.capacity) || empty.capacity>max_handles
            || !read(sap+0x140,boxes) || boxes>max_handles || !read(sap+0x148,endpoints) || endpoints<boxes*2+2
            || !read(sap+0x118,bitmap) || (empty.capacity && !bitmap)
            || !read(sap+0xb0,empty.bounds) || !read(sap+0xb8,empty.groups) || !read(sap+0xc0,empty.distances))return false;
        for(unsigned i=0;i<empty.capacity;++i) {unsigned char bit{};if(!read(bitmap+i,bit) || bit)return false;}
        output=empty;return true;
    }
    // Shipped150880/1569C0 packet, containing current native removals only.
    // Keep this immovable owner alive while native SAP retains its input
    // pointers, including after task completion and until the next update
    // replaces them or the scene is retired. No expected replay data is used.
    template<class Read> const Packet* BuildRemovalPacket(Read read) noexcept {
        if(state_!=State::Captured || !Detached(read))return nullptr;
        std::uintptr_t bounds_owner{},distance_owner{},bits{};unsigned words{};
        Packet packet{};
        if(!read(aabb_+0x108,bounds_owner) || !bounds_owner
            || !read(aabb_+0xa0,distance_owner) || !distance_owner
            || !read(bounds_owner+8,packet.bounds) || !packet.bounds
            || !read(bounds_owner+0x10,packet.capacity) || packet.capacity>max_handles
            || !read(aabb_+0x90,packet.groups) || !packet.groups
            || !read(distance_owner+8,packet.distances) || !packet.distances
            || !read(aabb_+0x58,bits) || !read(aabb_+0x60,words))return nullptr;
        words&=0x7fffffffu;
        if(words>removed_.size() || (words&&!bits))return nullptr;
        for(unsigned i=0;i<words;++i) {
            unsigned value{};if(!read(bits+i*4,value))return nullptr;
            for(unsigned b=0;b<32;++b)if(value&(1u<<b)) {
                const auto handle=i*32+b;
                if(handle>=packet.capacity || packet.removed_count==indices_.size())return nullptr;
                indices_[packet.removed_count++]=handle;
            }
        }
        if(!packet.removed_count && std::none_of(created_.begin(),created_.end(),[](auto value){return value!=0;}))return nullptr;
        packet.removed=packet.removed_count?indices_.data():nullptr;packet_=packet;state_=State::PacketPrepared;
        return &packet_;
    }
private:
    struct Actor {std::uintptr_t actor{},type{},sim{};};
    struct Aggregate {std::uintptr_t owner{},self{};unsigned slot{},handle{};};
    std::array<Aggregate,16> aggregates_{};
    std::array<std::uintptr_t,128> aggregate_slots_{};
    std::uintptr_t aggregate_table_{};
    unsigned aggregate_count_{},aggregate_extent_{},aggregate_free_{};
    std::array<Actor,max_actors> actors_{};
    std::array<unsigned,max_handles/32> removed_{};
    std::array<unsigned,max_handles/32> created_{};
    std::array<std::uintptr_t,4> id_owners_{};
    std::array<std::array<unsigned,max_handles/32>,4> deleted_ids_{};
    std::array<unsigned,max_handles/32> updated_{};
    std::uintptr_t module_{},scene_{},aabb_{},volumes_{},sap_{},islands_{};
    std::uint64_t deleted_nodes_{};
    // Recovered native bodies can be removed again before their first update.
    // Admit only their edge-free activation receipts (148140 consumes +198),
    // never pending edges, deactivation, or another owner's node work.
    template<class Read> bool OwnedActivationInputs(Read read) const noexcept {
        unsigned pending{};if(!read(islands_+0x38,pending) || pending)return false;
        for(unsigned slot=0;slot<2;++slot) {
            const auto graph=islands_+(slot?0x310u:0xb0u);
            std::uintptr_t nodes{},bits{},inputs{};unsigned count{},capacity{},words{},input_count{},input_capacity{};
            if(!read(graph+0x18,nodes) || !read(graph+0x20,count) || count>64
                || !read(graph+0x24,capacity) || count>(capacity&0x7fffffffu) || (count&&!nodes)
                || !read(graph+0x178,bits) || !read(graph+0x180,words))return false;
            words&=0x7fffffffu;if(words>max_handles/32 || (words&&!bits))return false;
            for(unsigned j=0;j<words;++j) {unsigned value{};if(!read(bits+j*4,value) || value)return false;}
            for(unsigned offset:{0x160u,0x170u,0x1a8u}) {unsigned value{};if(!read(graph+offset,value) || value)return false;}
            if(!read(graph+0x190,inputs) || !read(graph+0x198,input_count) || input_count>body_count_
                || !read(graph+0x19c,input_capacity) || input_count>(input_capacity&0x7fffffffu)
                || (input_count&&!inputs))return false;
            std::uint64_t seen{};
            for(unsigned i=0;i<input_count;++i) {
                unsigned handle{},edge{};unsigned char flags{};std::uintptr_t binding{};
                if(!read(inputs+i*4,handle) || (handle&63) || handle/64>=count)return false;
                const auto id=handle/64;
                const auto node=nodes+id*0x20;
                if(!(deleted_nodes_&(std::uint64_t{1}<<id)) || (seen&(std::uint64_t{1}<<id))
                    || !read(node,edge) || edge!=0xffffffffu || !read(node+4,flags) || flags!=0x20
                    || !read(node+0x18,binding))return false;
                bool owned{};for(unsigned j=count_-body_count_;j<count_;++j)owned|=binding==actors_[j].sim+0x60;
                if(!owned)return false;seen|=std::uint64_t{1}<<id;
            }
            for(unsigned id=0;id<count;++id) {
                unsigned char flags{};if(!read(nodes+id*0x20+4,flags) || (flags&0x10)
                    || ((flags&0x20) && !(seen&(std::uint64_t{1}<<id))))return false;
            }
        }
        return true;
    }
    template<class Read> static bool ReadNodeInputs(Read read,std::uintptr_t island,std::uintptr_t& nodes,
        unsigned& count,std::uintptr_t& bits,unsigned& words) noexcept {
        unsigned capacity{};
        if(!read(island+0x18,nodes) || !read(island+0x20,count) || count>64
            || !read(island+0x24,capacity) || count>(capacity&0x7fffffffu) || (count&&!nodes)
            || !read(island+0x178,bits) || !read(island+0x180,words))return false;
        words&=0x7fffffffu;
        if(words>max_handles/32 || (words&&!bits))return false;
        for(unsigned offset:{0x160u,0x170u,0x198u,0x1a8u}) {unsigned value{};if(!read(island+offset,value) || value)return false;}
        return true;
    }
    unsigned count_{},body_count_{},extent_{};
    enum class State {Empty,Captured,PacketPrepared};
    State state_{};
    Packet packet_{};
    std::array<unsigned,max_handles> indices_{};
    Check check_{};
    template<class Read> bool AggregateOwner(Read read,std::uintptr_t owner,Aggregate& value) const noexcept {
        unsigned members{},dirty{};std::uintptr_t self{};
        if(!owner || !read(owner,value.handle) || value.handle>=extent_
            || !read(owner+0x10,members) || members
            || !read(owner+0x20,dirty) || dirty!=0xffffffffu
            || !read(owner+0x18,self))return false;
        if(self) {
            std::uintptr_t type{},parent{};unsigned count{};std::uint8_t pending{};
            if(!read(self,type) || type!=module_+0x1acb50
                || !read(self+0x48,parent) || parent!=owner
                || !read(self+0x18,count) || count || !read(self+0x40,pending) || pending)return false;
        }
        value.owner=owner;value.self=self;return true;
    }
    template<class Read> bool CaptureAggregates(Read read) noexcept {
        unsigned capacity{};
        if(!read(aabb_+0x1c8,aggregate_count_) || aggregate_count_>aggregates_.size()
            || !read(aabb_+0x1d8,aggregate_extent_) || aggregate_extent_>aggregate_slots_.size()
            || aggregate_count_>aggregate_extent_ || !read(aabb_+0x1cc,aggregate_free_)
            || !read(aabb_+0x1d0,aggregate_table_) || !read(aabb_+0x1dc,capacity)
            || (capacity&0x7fffffffu)<aggregate_extent_ || (aggregate_extent_&&!aggregate_table_))return false;
        for(unsigned i=0;i<aggregate_extent_;++i)
            if(!read(aggregate_table_+i*8,aggregate_slots_[i]))return false;
        std::array<bool,128> free{};unsigned free_count{},slot=aggregate_free_;
        while(slot!=0xffffffffu) {
            if(slot>=aggregate_extent_ || free[slot] || aggregate_slots_[slot]>0xffffffffu)return false;
            free[slot]=true;++free_count;slot=static_cast<unsigned>(aggregate_slots_[slot]);
        }
        if(free_count+aggregate_count_!=aggregate_extent_)return false;
        unsigned count{};
        for(unsigned i=0;i<aggregate_extent_;++i)if(!free[i]) {
            if(count==aggregates_.size())return false;
            auto& row=aggregates_[count++];row.slot=i;
            if(!AggregateOwner(read,aggregate_slots_[i],row))return false;
            for(unsigned previous=0;previous+1<count;++previous)
                if(aggregates_[previous].owner==row.owner || aggregates_[previous].handle==row.handle)return false;
        }
        return count==aggregate_count_ && AggregateWorkIdle(read);
    }
    template<class Read> bool AggregateWorkIdle(Read read) const noexcept {
        // Existing completed-boundary admission: no pending aggregate maps,
        // dirty sort work or overlap output arrays for native150CF0 to consume.
        for(unsigned offset:{0x1e8u,0x22cu,0x264u,0x118u,0x128u}) {
            unsigned count{};if(!read(aabb_+offset,count) || count)return false;
        }
        return true;
    }
    template<class Read> bool AggregatesMatch(Read read) const noexcept {
        std::uintptr_t table{};unsigned count{},extent{},head{};
        if(!read(aabb_+0x1c8,count) || count!=aggregate_count_
            || !read(aabb_+0x1d8,extent) || extent!=aggregate_extent_
            || !read(aabb_+0x1cc,head) || head!=aggregate_free_
            || !read(aabb_+0x1d0,table) || table!=aggregate_table_ || !AggregateWorkIdle(read))return false;
        for(unsigned i=0;i<extent;++i) {
            std::uintptr_t slot{};if(!read(table+i*8,slot) || slot!=aggregate_slots_[i])return false;
        }
        for(unsigned i=0;i<count;++i) {
            Aggregate row{};const auto& saved=aggregates_[i];
            if(!AggregateOwner(read,saved.owner,row) || row.self!=saved.self || row.handle!=saved.handle)return false;
        }
        return true;
    }
    template<class Read> bool EmptyAggregateVolume(Read read,unsigned handle,std::uintptr_t user,unsigned tag) const noexcept {
        if(!(tag&1) || !user || (user&15))return false;
        bool found{};
        for(unsigned i=0;i<aggregate_count_;++i)
            found|=aggregates_[i].slot==tag/2 && aggregates_[i].handle==handle;
        if(!found)return false;
        std::uintptr_t groups{},distance_owner{},distances{};unsigned group{},distance{};
        if(!read(aabb_+0x90,groups) || !groups || !read(groups+handle*4,group) || group==0xffffffffu
            || !read(aabb_+0xa0,distance_owner) || !distance_owner || !read(distance_owner+8,distances)
            || !distances || !read(distances+handle*4,distance) || distance)return false;
        for(unsigned axis=0;axis<3;++axis) {
            std::uintptr_t endpoints{};unsigned low{},high{};
            if(!read(sap_+0xd0+axis*8,endpoints) || !endpoints
                || !read(endpoints+handle*8,low) || low!=0x3fffffffu
                || !read(endpoints+handle*8+4,high) || high!=0x3fffffffu)return false;
        }
        for(unsigned offset:{0x48u,0x58u,0x68u}) {
            std::uintptr_t bits{};unsigned words{},value{};
            if(!read(aabb_+offset,bits) || !read(aabb_+offset+8,words))return false;
            words&=0x7fffffffu;if(words>removed_.size() || (words&&!bits))return false;
            if(handle/32<words && (!read(bits+(handle/32)*4,value) || (value&(1u<<(handle%32)))))return false;
        }
        return true;
    }
    template<class Read> bool PendingUpdatesPreserved(Read read) const noexcept {
        std::uintptr_t data{};unsigned words{};
        if(!read(aabb_+0x68,data) || !read(aabb_+0x70,words))return false;
        words&=0x7fffffffu;
        if(words>updated_.size() || (words&&!data))return false;
        for(unsigned i=0;i<updated_.size();++i) {
            unsigned value{};if(i<words && !read(data+i*4,value))return false;
            // Native removal clears updates for its own removed handles.
            // Other pending work must remain pending, not be replayed early.
            if(value!=(updated_[i]&~removed_[i]))return false;
        }
        return true;
    }
    template<class Read> bool Members(Read read) const noexcept {
        std::uintptr_t type{},aabb{},table{},sap{};unsigned count{},phase{};
        if(!read(scene_,type) || type!=module_+0x19d008 || !read(scene_+0x738,aabb) || aabb!=aabb_
            || !read(aabb+0x100,sap) || sap!=sap_ || !read(sap,type) || type!=module_+0x1ace30
            || !read(scene_+0x1f14,phase) || phase || !read(scene_+0x2598,count)
            || count!=count_-body_count_ || !read(scene_+0x2590,table) || (count&&!table))return false;
        for(unsigned i=0;i<count_;++i) {
            const auto& row=actors_[i];std::uintptr_t actor{},sim{},kind{};
            if(i<count && (!read(table+i*8,actor) || actor!=row.actor))return false;
            if(!read(row.actor,kind) || kind!=row.type || !read(row.actor+0x80,sim)
                || sim!=(i<count?row.sim:0))return false;
        }
        return AggregatesMatch(read);
    }
    template<class Read> bool Bits(Read read,unsigned offset,bool removed) const noexcept {
        std::uintptr_t data{};unsigned words{};
        if(!read(aabb_+offset,data) || !read(aabb_+offset+8,words))return false;
        words&=0x7fffffffu;
        if(words>removed_.size() || (words&&!data))return false;
        for(unsigned i=0;i<removed_.size();++i) {
            unsigned value{};if(i<words && !read(data+i*4,value))return false;
            if(value!=(removed?(removed_[i]&~created_[i]):0u))return false;
        }
        return true;
    }
};
}
