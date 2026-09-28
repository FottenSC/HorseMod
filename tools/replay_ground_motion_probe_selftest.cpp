#include "ReplayGroundPose.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <span>
#include <limits>
using Horse::Deterministic::ReplayGroundPose;
template<class Pose,class Read,class Move>
bool probe(Read read,Move move,std::span<const std::uintptr_t> objects,unsigned& changed) {
    if constexpr(requires {Pose::PerturbNative(read,move,objects,changed);})
        return Pose::PerturbNative(read,move,objects,changed);
    changed=0;return false;
}
int main() {
    std::array<std::array<std::byte,0x300>,2> objects{};
    std::array<std::uintptr_t,2> addresses{};
    const std::array<float,12> initial{0,0,0,1,10,20,30,0,1,1,1,0};
    for(unsigned i=0;i<objects.size();++i) {
        addresses[i]=reinterpret_cast<std::uintptr_t>(objects[i].data());
        std::memcpy(objects[i].data()+0x270,initial.data(),48);
    }
    const auto original=objects;
    const auto read=[&](std::uintptr_t p,auto& value) {
        for(const auto address:addresses)if(p>=address && p+sizeof(value)<=address+0x300) {
            std::memcpy(&value,reinterpret_cast<const void*>(p),sizeof(value));return true;
        }
        return false;
    };
    unsigned calls{},changed{};
    const auto move=[&](std::uintptr_t object,const auto& delta,const auto& quaternion) {
        ++calls;std::array<float,12> pose{};assert(read(object+0x270,pose));
        assert(std::memcmp(pose.data(),quaternion.data(),16)==0);
        for(unsigned i=0;i<3;++i)pose[4+i]+=delta[i];
        std::memcpy(reinterpret_cast<void*>(object+0x270),pose.data(),48);return true;
    };
    if(!probe<ReplayGroundPose>(read,move,addresses,changed)) {
        std::puts("motion diagnostic did not drive native movement");return 47;
    }
    assert(calls==2 && changed==2);
    for(unsigned i=0;i<objects.size();++i) {
        assert(std::memcmp(objects[i].data(),original[i].data(),0x280)==0);
        assert(std::memcmp(objects[i].data()+0x28c,original[i].data()+0x28c,0x74)==0);
    }
    assert(objects!=original);
    objects=original;calls=changed=0;
    auto duplicated=addresses;duplicated[1]=duplicated[0];
    assert(!probe<ReplayGroundPose>(read,move,duplicated,changed) && !calls && !changed);
    assert(!probe<ReplayGroundPose>(read,move,{},changed) && !calls && !changed);
    const float invalid=std::numeric_limits<float>::quiet_NaN();
    std::memcpy(objects[1].data()+0x280,&invalid,4);
    assert(!probe<ReplayGroundPose>(read,move,addresses,changed) && !calls && !changed);
    objects=original;
    const auto no_effect=[&](auto,const auto&,const auto&){++calls;return true;};
    assert(!probe<ReplayGroundPose>(read,no_effect,addresses,changed) && calls==1 && changed==0);
    calls=changed=0;
    const auto fail_second=[&](auto object,const auto& delta,const auto& quaternion) {
        if(calls==1){++calls;return false;}return move(object,delta,quaternion);
    };
    assert(!probe<ReplayGroundPose>(read,fail_second,addresses,changed) && calls==2 && changed==1);
}
