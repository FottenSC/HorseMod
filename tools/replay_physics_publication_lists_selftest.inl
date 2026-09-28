#include "deterministic/ReplayPhysicsPublicationLists.hpp"
#define REQUIRE(...) expect((__VA_ARGS__), "physics publication lists: " #__VA_ARGS__)
static void TestPhysicsPublicationLists() {
    using Lists=Horse::Deterministic::ReplayPhysicsPublicationLists;
    using Array=Lists::Array;
    const auto read=[](std::uintptr_t p,auto& value){std::memcpy(&value,reinterpret_cast<void*>(p),sizeof(value));return true;};
    const auto write=[](std::uintptr_t p,const auto& value){std::memcpy(reinterpret_cast<void*>(p),&value,sizeof(value));return true;};
    for(unsigned fault=0;fault<7;++fault) {
        std::array<std::byte,0x400> owner{};
        std::array<std::uintptr_t,3> b{101,202,0},c{303,404,0};
        const auto address=reinterpret_cast<std::uintptr_t>(owner.data());
        const Array original{reinterpret_cast<std::uintptr_t>(b.data()),3,3};
        write(address+0x310,original);
        const std::array<std::uintptr_t,2> bodies{101,202};
        Lists lists;
        if(fault==1)b[1]=999; // A foreign future consumer cannot be hidden.
        if(fault==2)b[1]=101;
        if(fault==3)write(address+0x330,original); // Shared native backing.
        const bool prepared=lists.Prepare(address,bodies,read);
        REQUIRE(prepared==(fault!=1 && fault!=2 && fault!=3));
        if(!prepared)continue;
        REQUIRE(lists.owned_bytes()>=2*Lists::maximum_capacity*8+3*8);
        unsigned writes{};
        const auto controlled_write=[&](std::uintptr_t p,const auto& v){++writes;if(fault==4 && writes==2)return false;return write(p,v);};
        const bool detached=lists.Detach(read,controlled_write);
        REQUIRE(detached==(fault!=4));
        if(!detached) {REQUIRE(lists.phase()==Lists::Phase::Poisoned && !lists.Detach(read,write));continue;}
        REQUIRE(b==std::array<std::uintptr_t,3>{101,202,0});
        write(address+0x310,Array{reinterpret_cast<std::uintptr_t>(c.data()),2,3});
        if(fault==5)b[0]=707; // Frozen B storage changed: no writes/free allowed.
        unsigned releases{};const auto release=[&](std::uintptr_t p){++releases;REQUIRE(p==reinterpret_cast<std::uintptr_t>(c.data()));return fault!=6;};
        const bool recovered=lists.Recover(read,write,release);
        REQUIRE(recovered==(fault!=5 && fault!=6));
        if(fault==5) {REQUIRE(!releases);continue;}
        if(fault==6) {REQUIRE(lists.phase()==Lists::Phase::Poisoned && !lists.Recover(read,write,release) && releases==1);continue;}
        Array restored{};read(address+0x310,restored);REQUIRE(restored==original && releases==1);
        REQUIRE(lists.Recover(read,write,release) && releases==1);
    }
    for(unsigned fault=0;fault<4;++fault) {
        std::array<std::byte,0x400> owner{};
        std::array<std::uintptr_t,2> b{101,202},c{303,404};
        const auto address=reinterpret_cast<std::uintptr_t>(owner.data());
        const Array original{reinterpret_cast<std::uintptr_t>(b.data()),2,2};
        const Array current{reinterpret_cast<std::uintptr_t>(c.data()),2,2};
        write(address+0x310,original);Lists lists;
        REQUIRE(lists.Prepare(address,b,read));REQUIRE(!lists.CanCommit(read));
        REQUIRE(lists.Detach(read,write));write(address+0x310,current);
        if(fault==1)write(address+0x310,original);
        if(fault==2)b[0]=999;
        unsigned releases{};
        const auto release=[&](std::uintptr_t p){++releases;REQUIRE(p==original.data);return fault!=3;};
        const auto before=owner;const bool committed=lists.Commit(read,release);
        REQUIRE(committed==(fault==0));REQUIRE(owner==before);
        if(fault==0) {
            REQUIRE(lists.phase()==Lists::Phase::Committed && releases==1);
            REQUIRE(lists.Commit(read,release) && releases==1);
            REQUIRE(!lists.Recover(read,write,release));
        } else if(fault==3) {
            REQUIRE(lists.phase()==Lists::Phase::Poisoned && releases==1);
            REQUIRE(!lists.Commit(read,release) && !lists.Recover(read,write,release) && releases==1);
        } else REQUIRE(!releases);
    }
}
#undef REQUIRE
