#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <cassert>
namespace RC::Unreal {
constexpr std::uint64_t CPF_Transient=0x2000,CPF_DuplicateTransient=0x200000,
    CPF_InstancedReference=0x80000,CPF_ContainsInstancedReference=0x8000000000;
enum EStructFlags {STRUCT_SerializeNative=0x800,STRUCT_PostSerializeNative=0x40000};
struct Name {std::wstring value; auto ToString(){return value;}};
struct Class {std::wstring value;auto GetFName(){return Name{value};}};
struct FProperty {
    int offset{},size{8},dim{1};std::uint64_t flags{};std::wstring kind=L"ObjectProperty";
    int GetOffset_Internal(){return offset;} int GetElementSize(){return size;} int GetArrayDim(){return dim;}
    auto GetPropertyFlags(){return flags;} auto GetClass(){return Class{kind};}
    // Native multicast Identical F59120 returns false for nonempty delegates
    // with port1000 even when the two invocation lists have identical entries.
    bool Identical_InContainer(const void* a,const void* b,int,unsigned){
        return kind!=L"MulticastDelegateProperty" && !std::memcmp(static_cast<const char*>(a)+offset,static_cast<const char*>(b)+offset,size);
    }
};
struct FArrayProperty:FProperty {FProperty* inner{};auto GetInner(){return inner;}};
struct Struct {int flags{};void* ops{};std::vector<FProperty*> fields;
    bool HasAnyStructFlags(EStructFlags mask){return (flags&mask)!=0;}
    int GetStructFlags(){return flags;}void* GetCppStructOps(){return ops;}
    auto& ForEachPropertyInChain(){return fields;}
};
struct FStructProperty:FProperty {Struct* type{};auto GetStruct(){return type;}};
struct UObject {Struct* type{};std::uint64_t value{};auto GetClassPrivate(){return type;}};
}
struct Sc6ReplayObjectLease {bool valid=true;struct Status{bool value;bool ok()const{return value;}};
    Status ValidateObject(void* p)const{return {valid && p};}};
struct Probe {
struct Witness {bool created=true,detached=true,destroyed{},lease_released{};
    std::uintptr_t copy{},source=100,outer=200;int source_index=3,source_serial=4;unsigned properties=1;};
static inline bool cold=true;
static bool Cold(std::uintptr_t,std::uintptr_t,std::uintptr_t){return cold;}
template<class T>static T Read(std::uintptr_t p,std::size_t off=0){T v;std::memcpy(&v,reinterpret_cast<void*>(p+off),sizeof(v));return v;}
#include "particle_copy_bounds.inl"
#include "particle_copy_single_gpu.inl"
#include "particle_copy_outer_modify.inl"
#include "particle_copy_equivalence.inl"
};
int main() {
    using namespace RC::Unreal;
    FProperty p;p.offset=offsetof(UObject,value);p.kind=L"NameProperty";
    Struct type_a;type_a.fields={&p};UObject a{&type_a,17},b{&type_a,17};
    Probe::Witness wa,wb;wa.copy=reinterpret_cast<std::uintptr_t>(&a);wb.copy=reinterpret_cast<std::uintptr_t>(&b);
    Sc6ReplayObjectLease la,lb;
    const auto equivalent=[&]{return Probe::EquivalentDetached(0,wa,la,wb,lb);};
    assert(equivalent());++b.value;assert(!equivalent());--b.value;
    ++wb.source_serial;assert(!equivalent());--wb.source_serial;
    ++wb.source_index;assert(!equivalent());--wb.source_index;
    ++wb.source;assert(!equivalent());--wb.source;
    ++wb.outer;assert(!equivalent());--wb.outer;
    wb.destroyed=true;assert(!equivalent());wb.destroyed=false;
    wb.lease_released=true;assert(!equivalent());wb.lease_released=false;
    lb.valid=false;assert(!equivalent());lb.valid=true;
    Probe::cold=false;assert(!equivalent());Probe::cold=true;
    Struct type_b=type_a;b.type=&type_b;assert(!equivalent());b.type=&type_a;
    ++wb.properties;assert(!equivalent());--wb.properties;
    p.offset=0xad0;assert(!equivalent());p.offset=offsetof(UObject,value);
    p.dim=2;assert(!equivalent());p.dim=1;
    assert(equivalent() && a.value==17 && b.value==17); // Read-only comparison.
    std::array<std::uintptr_t,8> storage{};std::size_t bytes{};
    auto check=[&](FProperty& p){bytes=0;return Probe::BoundedProperty(0,&p,reinterpret_cast<std::uintptr_t>(storage.data()),sizeof(storage),0,bytes);};
    FProperty strong;strong.flags=CPF_InstancedReference;
    if(!check(strong))return 1;
    storage[0]=123;if(check(strong))return 2;
    strong.kind=L"WeakObjectProperty";if(!check(strong))return 3;
    FProperty inner;
    FArrayProperty array;array.kind=L"ArrayProperty";array.size=16;array.inner=&inner;array.flags=CPF_ContainsInstancedReference;
    storage={};if(!check(array))return 4;
    std::array<std::uintptr_t,2> values{};
    storage[0]=reinterpret_cast<std::uintptr_t>(values.data());storage[1]=(std::uintptr_t(2)<<32)|1;
    if(check(array))return 5;
    array.flags=0;if(!check(array) || bytes!=16)return 6;
    storage[1]=(std::uintptr_t(65)<<32)|1;if(check(array))return 7;
    storage[1]=(std::uintptr_t(1)<<32)|2;if(check(array))return 8;
    storage[1]=(std::uintptr_t(2)<<32)|1;storage[0]=0;if(check(array))return 9;
    FProperty delegate;delegate.kind=L"MulticastDelegateProperty";delegate.size=16;delegate.flags=CPF_InstancedReference;
    storage={};if(!check(delegate))return 10;
    storage[0]=reinterpret_cast<std::uintptr_t>(values.data());storage[1]=(std::uintptr_t(1)<<32)|1;
    if(!check(delegate)||bytes!=16)return 11;
    Struct type;FStructProperty nested;nested.kind=L"StructProperty";nested.size=16;nested.type=&type;
    FProperty scalar;scalar.kind=L"FloatProperty";scalar.size=4;type.fields={&scalar};
    if(!check(nested))return 12;
    scalar.offset=15;if(check(nested))return 13;
    scalar.offset=0;type.flags=STRUCT_SerializeNative;if(check(nested))return 14;
    type.flags=STRUCT_PostSerializeNative;if(check(nested))return 15;
    type.flags=0;type.fields={&nested};if(check(nested))return 16;
    FProperty unknown;unknown.kind=L"TextProperty";if(check(unknown))return 17;
    std::array<std::uintptr_t,0xae0/8> component{};
    std::uintptr_t gpu=0x394c100,cpu=0x3940000;
    std::array<std::uintptr_t,2> slots{0,reinterpret_cast<std::uintptr_t>(&gpu)};
    component[0xa50/8]=reinterpret_cast<std::uintptr_t>(slots.data());
    component[0xa58/8]=(std::uintptr_t(2)<<32)|2;
    const auto address=reinterpret_cast<std::uintptr_t>(component.data());
    if(!Probe::SingleGpuSource(0,address))return 18;
    slots[0]=slots[1];if(Probe::SingleGpuSource(0,address))return 19;
    slots[0]=reinterpret_cast<std::uintptr_t>(&cpu);if(Probe::SingleGpuSource(0,address))return 20;
    slots={};if(Probe::SingleGpuSource(0,address))return 21;
    // Actual Color metadata from the failed composed InstanceParameters walk.
    // Native F465E0 -> F48A60 serializes one uint32 without allocating an owner.
    std::array<std::uintptr_t,8> color_table{};
    std::uintptr_t color_ops=reinterpret_cast<std::uintptr_t>(color_table.data());
    const auto color_base=color_ops-0x354c750;
    color_table[7]=color_base+0xf465e0;
    type.flags=0xe838;type.ops=&color_ops;type.fields.clear();nested.size=4;
    auto color_check=[&](){bytes=0;return Probe::BoundedProperty(color_base,&nested,
        reinterpret_cast<std::uintptr_t>(storage.data()),sizeof(storage),0,bytes);};
    if(!color_check() || bytes)return 22;
    nested.size=12;if(color_check())return 23;nested.size=4;
    type.flags|=STRUCT_PostSerializeNative;if(color_check())return 24;type.flags=0xe838;
    color_table[7]+=1;if(color_check())return 25;color_table[7]-=1;
    color_ops+=8;if(color_check())return 26;
    // Serialized opaque UInt64 handles are admitted only when absent; copying
    // a nonzero native handle cannot establish independent ownership.
    FProperty handle;handle.kind=L"UInt64Property";handle.size=8;storage={};
    if(!check(handle))return 27;
    storage[0]=1;if(check(handle))return 28;storage[0]=0;
    handle.size=4;if(check(handle))return 29;
    // The exact WorldSettings actor Modify override reaches UObject Modify,
    // with no reflected-property transaction or root-component recursion in
    // this admitted domain. Native code is audited separately from this fixture.
    std::vector<std::byte> image(0x418b130);
    const auto native_base=reinterpret_cast<std::uintptr_t>(image.data());
    std::array<std::uintptr_t,0x170/8> outer{};
    outer[0]=native_base+0x3499188;
    auto* modify=reinterpret_cast<std::uintptr_t*>(image.data()+0x3499188+0x70);
    *modify=native_base+0x1c23160;
    const auto outer_address=reinterpret_cast<std::uintptr_t>(outer.data());
    if(!Probe::PassiveOuterModify(native_base,outer_address))return 30;
    outer[0x168/8]=1;if(Probe::PassiveOuterModify(native_base,outer_address))return 31;outer[0x168/8]=0;
    image[0x418b108]=std::byte{1};if(Probe::PassiveOuterModify(native_base,outer_address))return 32;image[0x418b108]=std::byte{0};
    image[0x418b12f]=std::byte{1};if(Probe::PassiveOuterModify(native_base,outer_address))return 33;image[0x418b12f]=std::byte{0};
    *modify+=1;if(Probe::PassiveOuterModify(native_base,outer_address))return 34;
    std::array<std::uint32_t,4> callback_a{123,456,789,0},callback_b=callback_a;
    std::array<std::uintptr_t,2> list_a{reinterpret_cast<std::uintptr_t>(callback_a.data()),(std::uintptr_t(4)<<32)|1};
    auto list_b=list_a;list_b[0]=reinterpret_cast<std::uintptr_t>(callback_b.data());list_b[1]=(std::uintptr_t(1)<<32)|1;
    auto delegate_check=[&](){return Probe::EquivalentProperty(&delegate,
        reinterpret_cast<std::uintptr_t>(list_a.data()),reinterpret_cast<std::uintptr_t>(list_b.data()),0);};
    if(!delegate_check())return 35;
    for(auto& part:callback_b){++part;if(delegate_check())return 36;--part;}
    list_b[0]=list_a[0];if(delegate_check())return 37;list_b[0]=reinterpret_cast<std::uintptr_t>(callback_b.data());
    list_b[1]=(std::uintptr_t(1)<<32)|2;if(delegate_check())return 38;
    list_b={};if(delegate_check())return 39;list_a={};if(!delegate_check())return 40;
}
