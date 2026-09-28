#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse::Deterministic {
// Owned result of native private construction, not an historical actor image.
// Engine queue absence and UObject lifetime are checked by the enclosing graph.
struct ReplayGroundPrivateBody {
    std::uintptr_t actor{},module{},body{};
    std::array<std::uintptr_t,2> body_actors{};
    std::array<std::int16_t,2> scene_ids{};
    unsigned shape_count{};
    std::array<std::uintptr_t,16> shapes{};
    template<class Read,class Scene> bool Validate(Read read,Scene scene)const noexcept {
        if(!actor || !module || !shape_count || shape_count>shapes.size())return false;
        std::uintptr_t table{},release{},get_scene{},user{},owner_scene{},simulation{};
        unsigned control{};std::uint16_t count{},handle_count{};
        if(!read(actor,table) || table!=module+0x19b7c0
            || !read(table,release) || release!=module+0x396a0
            || !read(table+0x30,get_scene) || get_scene!=module+0x3c8e0
            || !read(actor+0x10,user) || user!=(body?body+0x200:0)
            || !read(actor+0x60,owner_scene) || owner_scene
            || !read(actor+0x68,control) || (control&0xc0000000u)
            || !read(actor+0x80,simulation) || simulation
            || scene(actor)!=0 || !read(actor+0x30,count) || count!=shape_count
            || !read(actor+0x40,handle_count) || handle_count!=shape_count)return false;
        if(body) {
            unsigned tag{};std::uintptr_t self{};
            if(!read(body+0x200,tag) || tag!=1 || !read(body+0x208,self) || self!=body)return false;
            unsigned matches{};
            for(unsigned side=0;side<2;++side) {
                std::uintptr_t bound{};std::int16_t id{};
                if(!read(body+0xf0+side*8,bound) || bound!=body_actors[side]
                    || !read(body+0x10+side*2,id) || id!=scene_ids[side]
                    || (bound && !id))return false;
                matches+=bound==actor;
            }
            if(matches!=1)return false;
        }
        std::uintptr_t shape_storage{},handle_storage{};
        if(count==1)shape_storage=actor+0x28;
        else if(!read(actor+0x28,shape_storage) || !shape_storage)return false;
        if(handle_count==1)handle_storage=actor+0x38;
        else if(!read(actor+0x38,handle_storage) || !handle_storage)return false;
        for(unsigned i=0;i<count;++i) {
            std::uintptr_t shape{};std::uint64_t handle{};unsigned shape_control{};
            if(!read(shape_storage+i*8,shape) || !shape || shape!=shapes[i]
                || !read(handle_storage+i*8,handle) || handle!=0xffffffffu
                || !read(shape+0x38,shape_control) || (shape_control&0xc0000045u))return false;
            for(unsigned prior=0;prior<i;++prior)if(shapes[prior]==shape)return false;
        }
        return true;
    }
    template<class Read,class Scene> bool Capture(Read read,Scene scene,std::uintptr_t fresh,
        std::uintptr_t runtime,std::uintptr_t instance) noexcept {
        if(actor)return false;
        ReplayGroundPrivateBody next;next.actor=fresh;next.module=runtime;next.body=instance;
        if(instance)for(unsigned side=0;side<2;++side)
            if(!read(instance+0xf0+side*8,next.body_actors[side])
                || !read(instance+0x10+side*2,next.scene_ids[side]))return false;
        std::uint16_t count{};std::uintptr_t storage{};
        if(!fresh || !read(fresh+0x30,count) || !count || count>next.shapes.size())return false;
        next.shape_count=count;
        if(count==1)storage=fresh+0x28;
        else if(!read(fresh+0x28,storage) || !storage)return false;
        for(unsigned i=0;i<count;++i)if(!read(storage+i*8,next.shapes[i]))return false;
        if(!next.Validate(read,scene))return false;
        *this=next;return true;
    }
};

// Engine ownership used by both CreateBodyActorsAndShapes and TermBody.
// A private SDK actor has no scene membership, but its engine scene ID still
// selects the scene lock and FPhysScene consumer owner during destruction.
struct ReplayGroundPrivateScene {
    std::uintptr_t module{},world{},owner{},helper{};
    std::array<std::uintptr_t,2> scenes{},user_data{};
    std::array<std::int16_t,2> ids{};
    unsigned selector_count{};unsigned char asynchronous{};
    template<class Read,class Resolve> bool Validate(Read read,Resolve resolve)const noexcept {
        if(!module || !world || !owner || !helper || !scenes[0]
            || !selector_count || selector_count>3 || (asynchronous && selector_count<3))return false;
        std::uintptr_t value{};unsigned count{};unsigned char enabled{};
        if(!read(world+0x1c8,value) || value!=owner || !read(helper+0x20,value) || value!=owner
            || !read(owner+4,count) || count!=selector_count || !read(owner,enabled) || enabled!=asynchronous)return false;
        for(unsigned side=0;side<2;++side) {
            if(!read(helper+0x88+side*8,value) || value!=scenes[side])return false;
            if(side && !asynchronous) {
                if(scenes[side] || ids[side] || user_data[side])return false;
                continue;
            }
            std::int16_t id{};std::uintptr_t table{},data{};unsigned tag{};
            if(!scenes[side] || !read(owner+0xf8+side*4,id) || !id || id!=ids[side]
                || resolve(id)!=scenes[side] || !read(scenes[side],table) || table!=module+0x19d008
                || !read(table+0x330,value) || value!=module+0x431d0
                || !read(table+0x338,value) || value!=module+0x432b0
                || !read(scenes[side]+8,data) || !data || data!=user_data[side]
                || !read(data,tag) || tag!=3 || !read(data+8,value) || value!=owner)return false;
        }
        return true;
    }
    bool Matches(const ReplayGroundPrivateBody& body)const noexcept {
        return body.body && body.module==module && body.scene_ids==ids;
    }
    template<class Read,class Resolve> bool Capture(Read read,Resolve resolve,std::uintptr_t runtime,
        std::uintptr_t live_world,std::uintptr_t phys_scene,std::uintptr_t init_helper) noexcept {
        if(module)return false;
        ReplayGroundPrivateScene next;next.module=runtime;next.world=live_world;next.owner=phys_scene;next.helper=init_helper;
        if(!phys_scene || !init_helper || !read(phys_scene+4,next.selector_count)
            || !read(phys_scene,next.asynchronous))return false;
        for(unsigned side=0;side<2;++side) {
            if(!read(init_helper+0x88+side*8,next.scenes[side]))return false;
            if(side && !next.asynchronous)continue;
            if(!next.scenes[side] || !read(phys_scene+0xf8+side*4,next.ids[side])
                || !read(next.scenes[side]+8,next.user_data[side]))return false;
        }
        if(!next.Validate(read,resolve))return false;
        *this=next;return true;
    }
};
}
