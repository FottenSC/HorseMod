#pragma once
#include "ReplayGroundPrivateBody.hpp"
#include <array>
#include <cstring>
#include <utility>

namespace Horse::Deterministic {
// Historical shape values, with explicit engine filter identities. Geometry,
// user data and native materials are comparison witnesses, never pointer-write
// payloads. The enclosing graph leases the source BodySetup/material assets;
// freshly constructed shapes must independently own those same dependencies.
struct ReplayGroundShapeState {
    struct Binding {unsigned component{},owner{};};
    struct MaterialValues {
        std::array<std::byte,15> values{};
        std::uintptr_t user{};
        bool operator==(const MaterialValues&)const noexcept=default;
    };
    struct Row {
        std::array<std::byte,0x7a> values{};
        std::uintptr_t user{};
        std::array<std::uintptr_t,16> materials{};
        std::array<MaterialValues,16> material_values{};
        unsigned material_count{};
        template<class T> T Value(unsigned offset)const noexcept {
            T out{};std::memcpy(&out,values.data()+offset,sizeof(out));return out;
        }
    };
    std::array<Row,16> rows{};
    Binding source{};
    unsigned count{};
    bool ready{};
    template<class Read> static bool ReadMaterial(Read read,std::uintptr_t material,std::uintptr_t module,MaterialValues& output) noexcept {
        std::uintptr_t table{};
        if(!material || !read(material,table) || table!=module+0x199b40)return false;
        constexpr std::array<std::pair<unsigned,unsigned>,6> getters{{
            {0x40,0x32500},{0x50,0x32530},{0x60,0x325d0},
            {0x78,0x32640},{0x88,0x32680},{0x98,0x326b0}}};
        for(const auto [slot,rva]:getters) {
            std::uintptr_t entry{};if(!read(table+slot,entry) || entry!=module+rva)return false;
        }
        // The verified getters consume friction/restitution floats+30/34/38,
        // ushort flags+3c and the two combine-mode nibbles+3e. Ignore refcounts,
        // the material-table index, and unknown padding at+3f. Shared values
        // are dependency witnesses only; historical values never mutate them.
        return read(material+0x30,output.values) && read(material+0x10,output.user);
    }
    static bool FilterIdentity(const Row& row,Binding binding) noexcept {
        if(!binding.component || !binding.owner)return false;
        const auto simulation=row.Value<std::array<unsigned,4>>(0x50);
        const auto query=row.Value<std::array<unsigned,4>>(0x60);
        if(simulation==std::array<unsigned,4>{} && query==std::array<unsigned,4>{})return true;
        return simulation[0]<=0xffffu && simulation[2]==binding.component && query[0]==binding.owner;
    }
    template<class Read> static bool ReadRow(Read read,std::uintptr_t shape,std::uintptr_t module,Row& row) noexcept {
        std::uintptr_t table{};unsigned control{};
        if(!shape || !read(shape,table) || table!=module+0x19ee00
            || !read(shape+0x38,control) || (control&0x7fu))return false;
        constexpr std::array<std::pair<unsigned,unsigned>,16> slots{{
            {0x40,0x550a0},{0x90,0x55440},{0xa0,0x554a0},{0xb0,0x554e0},
            {0xc0,0x55650},{0xc8,0x55680},{0xe0,0x55810},{0xf0,0x55840},
            {0x108,0x55920},{0x110,0x55950},{0x98,0x55490},{0xa8,0x554d0},
            {0xd8,0x55800},{0xe8,0x55830},{0x100,0x558d0},{0x30,0x54fa0}}};
        for(const auto [slot,rva]:slots) {
            std::uintptr_t entry{};if(!read(table+slot,entry) || entry!=module+rva)return false;
        }
        alignas(16) std::array<std::byte,0xa0> values{};
        reinterpret_cast<void*(*)(void*,std::uintptr_t)>(module+0x88a0)(values.data(),shape);
        std::memcpy(row.values.data(),values.data(),row.values.size());
        std::memcpy(&row.user,values.data()+0x90,8);
        const auto kind=row.Value<unsigned>(0);
        if((kind!=0 && kind!=2 && kind!=3 && kind!=4 && kind!=5)
            || row.values[0x79]!=std::byte{1} || (std::to_integer<unsigned>(row.values[0x78])&6u))return false;
        row.material_count=reinterpret_cast<unsigned short(*)(std::uintptr_t)>(module+0x55650)(shape);
        if(!row.material_count || row.material_count>row.materials.size())return false;
        if(reinterpret_cast<unsigned(*)(std::uintptr_t,void*,unsigned,unsigned)>(module+0x55680)
            (shape,row.materials.data(),row.material_count,0)!=row.material_count)return false;
        for(unsigned i=0;i<row.material_count;++i)
            if(!ReadMaterial(read,row.materials[i],module,row.material_values[i]))return false;
        for(unsigned offset=0x34;offset<0x50;offset+=4)
            if((row.Value<unsigned>(offset)&0x7f800000u)==0x7f800000u)return false;
        return (row.Value<unsigned>(0x70)&0x7f800000u)!=0x7f800000u
            && (row.Value<unsigned>(0x74)&0x7f800000u)!=0x7f800000u;
    }
    static bool SameDependencies(const Row& a,const Row& b) noexcept {
        const auto kind=a.Value<unsigned>(0);
        if(kind!=b.Value<unsigned>(0) || a.user!=b.user || a.material_count!=b.material_count
            || a.materials!=b.materials || a.material_values!=b.material_values
            || a.values[0x79]!=b.values[0x79])return false;
        // PxGeometryHolder starts+4. Triangle geometry has unknown alignment
        // bytes between its byte flags+20 and mesh pointer+28; never compare or
        // install those bytes. Convex geometry's last meaningful byte is+28.
        const unsigned extent=kind==0?8:kind==2?12:kind==3?16:kind==4?41:33;
        if(std::memcmp(a.values.data()+4,b.values.data()+4,extent))return false;
        return kind!=5 || !std::memcmp(a.values.data()+0x2c,b.values.data()+0x2c,8);
    }
    template<class Read> bool Capture(Read read,std::uintptr_t actor,std::uintptr_t module,Binding binding) noexcept {
        if(ready || !binding.component || !binding.owner)return false;
        std::uint16_t shapes{};std::uintptr_t storage{};
        if(!read(actor+0x30,shapes) || !shapes || shapes>rows.size())return false;
        if(shapes==1)storage=actor+0x28;
        else if(!read(actor+0x28,storage) || !storage)return false;
        std::array<std::uintptr_t,16> identities{};
        for(unsigned i=0;i<shapes;++i) {
            if(!read(storage+i*8,identities[i]) || !ReadRow(read,identities[i],module,rows[i])
                || !FilterIdentity(rows[i],binding))return false;
            for(unsigned j=0;j<i;++j)if(identities[i]==identities[j])return false;
        }
        count=shapes;source=binding;ready=true;return true;
    }
    Row Rebound(unsigned i,Binding destination)const noexcept {
        auto row=rows[i];
        if(row.Value<std::array<unsigned,4>>(0x50)!=std::array<unsigned,4>{}
            || row.Value<std::array<unsigned,4>>(0x60)!=std::array<unsigned,4>{}) {
            std::memcpy(row.values.data()+0x58,&destination.component,4);
            std::memcpy(row.values.data()+0x60,&destination.owner,4);
        }
        return row;
    }
    template<class Read,class Write,class Scene> bool Install(Read read,Write write,Scene scene,
        const ReplayGroundPrivateBody& owner,Binding destination,unsigned* mismatch)const noexcept {
        if(mismatch)*mismatch=0xfdff0001u;
        if(!ready || count!=owner.shape_count || !owner.Validate(read,scene)
            || !destination.component || !destination.owner)return false;
        // Validate the whole immutable graph and both identity domains before
        // the first mutation. An old component index must never authorize a
        // new shape or silently become simulation policy.
        for(unsigned i=0;i<count;++i) {
            Row actual;
            if(mismatch)*mismatch=0xfd000001u|(i<<16);
            if(!ReadRow(read,owner.shapes[i],owner.module,actual)
                || !FilterIdentity(rows[i],source) || !FilterIdentity(actual,destination)
                || !SameDependencies(rows[i],actual))return false;
        }
        for(unsigned i=0;i<count;++i) {
            const auto row=Rebound(i,destination);const auto shape=owner.shapes[i],module=owner.module;
            reinterpret_cast<void(*)(std::uintptr_t,const void*)>(module+0x55490)(shape,row.values.data()+0x50);
            reinterpret_cast<void(*)(std::uintptr_t,const void*)>(module+0x554d0)(shape,row.values.data()+0x60);
            reinterpret_cast<void(*)(std::uintptr_t,float)>(module+0x55800)(shape,row.Value<float>(0x70));
            reinterpret_cast<void(*)(std::uintptr_t,float)>(module+0x55830)(shape,row.Value<float>(0x74));
            reinterpret_cast<void(*)(std::uintptr_t,const void*)>(module+0x558d0)(shape,row.values.data()+0x78);
            // Getter55440 consumes this exact local pose when no pending pose
            // write exists. Setter55370 renormalizes the quaternion, so a
            // setter round trip cannot establish exact historical state.
            std::array<std::byte,28> pose{};std::memcpy(pose.data(),row.values.data()+0x34,28);
            if(!owner.Validate(read,scene) || !write(shape+0x70,pose))return false;
            Row actual;
            if(!ReadRow(read,shape,module,actual) || !SameDependencies(row,actual))return false;
            for(unsigned offset=0x34;offset<0x7a;++offset)if(row.values[offset]!=actual.values[offset]) {
                if(mismatch)*mismatch=0xfd000000u|(i<<16)|offset;return false;
            }
        }
        if(!owner.Validate(read,scene))return false;
        if(mismatch)*mismatch=0;return true;
    }
};
}
