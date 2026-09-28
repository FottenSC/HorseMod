// The generated include contains the actual production Add, accounting and
// ReleaseCapture methods. Substitute framework owners, not payload operations.
#define NOMINMAX
#include <Windows.h>
#include <cstring>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#ifndef REPLAY_TRACE_MATERIAL_ADMISSION
struct Status {bool valid{true};bool ok() const{return valid;}static Status success(){return {};}};
struct Capture {
    struct Image {std::byte* address{};std::vector<std::byte> bytes;std::uint32_t mutable_bits{};};
    using Object=void;
    struct Id {Object* object{};};
    struct Root {Id component,chara,manager,scene;};
    template<class T>static T& At(void* p,std::size_t o){return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+o);}
    Id Identify(Object* p){return {p};}
struct State {unsigned value{};};struct Ref {unsigned value{};};
    struct Lease {bool release_ok{true};std::size_t owned_bytes() const{return sizeof(*this);}Status Release(){return {release_ok};}} lease_;
    struct Dynamic {std::vector<std::byte> bytes;};
    std::vector<Root> roots_;std::vector<State> states_;std::vector<Ref> expired_;
    std::vector<Image> values_,bindings_;std::array<Dynamic,2> dynamic_;
    std::size_t payload_bytes_{},expired_bytes_{},budget_{4*1024*1024},dynamic_count_{};
    bool captured_{},validation_only_{},release_expired_ok{true};std::uintptr_t base_{};void* world_{};unsigned thread_{};
    const void* survivor_source_{};
    Status Fail(const char*){return {false};}bool ReleaseExpired(){return release_expired_ok;}
#include "replay_trace_capture_methods.inl"
};
int main(){
    // The production SceneTransform call also admits the trace attachment
    // reached from OnVFxFinished. Empty delegates alone do not own overrides.
    std::array<std::uintptr_t,0x310/8> attachment_table{};
    alignas(16) std::array<std::byte,0x480> attachment{};
    const auto attachment_base=reinterpret_cast<std::uintptr_t>(attachment_table.data())-0x3360370;
    Capture::At<std::uintptr_t>(attachment.data(),0)=reinterpret_cast<std::uintptr_t>(attachment_table.data());
    constexpr std::array<std::pair<unsigned,unsigned>,3> routes{{{0x238,0x1d41870},{0x278,0x1d7fbb0},{0x308,0x1d5dbe0}}};
    for(const auto [offset,rva]:routes)attachment_table[offset/8]=attachment_base+rva;
    Capture valid_attachment;valid_attachment.base_=attachment_base;
    if(!valid_attachment.SceneTransform(attachment.data()))return 30;
    for(const auto [offset,rva]:routes) {
        attachment_table[offset/8]=attachment_base+rva+16;
        Capture changed;changed.base_=attachment_base;
        if(changed.SceneTransform(attachment.data())) {
            std::printf("trace attachment admitted changed native dispatch slot%x\n",offset);return 31;
        }
        attachment_table[offset/8]=attachment_base+rva;
    }
    for(const auto [offset,rva]:routes) {
        const auto* entry=reinterpret_cast<std::byte*>(attachment_table.data())+offset;
        if(std::none_of(valid_attachment.bindings_.begin(),valid_attachment.bindings_.end(),
            [&](const auto& row){return row.address==entry && row.bytes.size()==8;}))return 32;
    }
    Capture::At<std::uintptr_t>(attachment.data(),0)+=8;
    Capture replaced_type;replaced_type.base_=attachment_base;
    if(replaced_type.SceneTransform(attachment.data()))return 33;
    Capture::At<std::uintptr_t>(attachment.data(),0)-=8;
    Capture::At<int>(attachment.data(),0x1c0)=1;
    Capture listener;listener.base_=attachment_base;
    if(listener.SceneTransform(attachment.data()))return 34;
    Capture::At<int>(attachment.data(),0x1c0)=0;
    if(!valid_attachment.SceneTransform(nullptr))return 35;
    // Exercise the production root-capture block with independent raw component
    // storage. Native8D8C40 reads the owner root +280 during trace activation.
    alignas(16) std::array<std::byte,0x500> component{},chara{},manager{},scene{};
    constexpr std::uintptr_t base=0x10000000;void* world=component.data();
    Capture::At<void*>(component.data(),0x1c8)=world;
    Capture::At<std::uintptr_t>(component.data(),0)=base+0x3360ca8;
    Capture::At<void*>(component.data(),0x490)=chara.data();
    Capture::At<void*>(chara.data(),0x458)=manager.data();
    Capture::At<void*>(chara.data(),0x168)=scene.data();
    Capture::At<void*>(manager.data(),0x3a8)=component.data();
    Capture::At<void*>(scene.data(),0x190)=chara.data();
    Capture::At<std::uint64_t>(scene.data(),0x280)=0x4347de77c3a2f939;
    Capture a;std::vector<void*> owners;
    if(!a.CaptureRoot(component.data(),world,base,owners).ok())return 20;
    Capture::At<std::uint64_t>(scene.data(),0x280)=0x434a52bfc3aa4911;
    Capture b;owners.clear();
    if(!b.CaptureRoot(component.data(),world,base,owners).ok())return 21;
    for(const auto& row:a.values_)if(!a.Write(row))return 22;
    if(Capture::At<std::uint64_t>(scene.data(),0x280)!=0x4347de77c3a2f939) {
        std::puts("owner-root origin was not restored before native trace activation");return 23;
    }
    for(unsigned repeat=0;repeat<2;++repeat) {
        for(const auto& row:b.values_)if(!b.Write(row))return 24;
        if(Capture::At<std::uint64_t>(scene.data(),0x280)!=0x434a52bfc3aa4911)return 25;
    }
    if(std::find(owners.begin(),owners.end(),scene.data())==owners.end())return 26;
    bool bound=false;for(const auto& row:a.bindings_)if(row.address==chara.data()+0x168 && row.bytes.size()==8)bound=true;
    if(!bound)return 27;

    Capture c;std::array<std::uint64_t,512> source{};
    for(unsigned i=0;i<source.size();++i){
        source[i]=i;
        if(!c.Add(i%2?c.values_:c.bindings_,&source[i],sizeof(source[i])) || !c.ValidatePayloadAccounting())return 1;
        std::size_t actual{};
        for(const auto* rows:{&c.values_,&c.bindings_})for(const auto& row:*rows)actual+=row.bytes.capacity();
        if(actual!=c.payload_bytes_ || c.owned_bytes()>c.budget_)return 2;
    }
    const auto bytes=c.owned_bytes(),payload=c.payload_bytes_;
    if(!c.Add(c.bindings_,&source[0],8) || c.Add(c.bindings_,&source[0],4)
        || c.Add(c.values_,nullptr,8) || !c.Add(c.values_,nullptr,0)
        || c.owned_bytes()!=bytes || c.payload_bytes_!=payload)return 3;
    c.budget_=bytes;
    // Same source may independently belong to values and bindings. Capacity
    // failure must not publish it or lose accounting for earlier images.
    if(c.Add(c.values_,&source[0],8) || !c.ValidatePayloadAccounting() || c.owned_bytes()!=bytes)return 4;
    c.budget_+=64*1024; // New vector metadata coexists with its old backing.
    if(!c.Add(c.values_,&source[0],8) || c.payload_bytes_!=payload+8)return 5;
    ++c.payload_bytes_;if(c.ValidatePayloadAccounting())return 6;--c.payload_bytes_;
    c.lease_.release_ok=false;
    if(c.ReleaseCapture().ok() || !c.ValidatePayloadAccounting() || c.values_.empty())return 7;
    c.lease_.release_ok=true;c.release_expired_ok=false;
    if(c.ReleaseCapture().ok() || !c.ValidatePayloadAccounting() || c.values_.empty())return 8;
    c.release_expired_ok=true;
    if(!c.ReleaseCapture().ok() || c.payload_bytes_ || !c.values_.empty() || !c.bindings_.empty()
        || !c.ValidatePayloadAccounting())return 9;
    c.budget_=4*1024*1024;
    if(!c.Add(c.values_,source.data(),sizeof(source)) || !c.ValidatePayloadAccounting())return 10;
    std::puts("Production trace capture accounting, capacity rejection and retained-owner release passed");
}
#else
// Narrow G1 fallback: actual capture and Prepare (called by PrepareStorage and
// Prepared::Publish via PrepareInitial). No setter, completion predicate, proxy
// lease, particle marker or render-task drain is supplied by this fixture.
#include "ReplayTraceWeakReference.hpp"
using Horse::Deterministic::ReplayTraceWeakController;
enum class FailureCode {RestorePreflightFailed};
struct Status {
    bool valid{true};bool ok()const{return valid;}
    static Status success(){return {};}static Status failure(FailureCode){return {false};}
};
struct TestObject {alignas(16) std::array<std::byte,0x1000> bytes{};
    int GetInternalIndex()const {int i;std::memcpy(&i,bytes.data()+0xc,4);return i;}
};
namespace RC {
enum class LogLevel {Default,Warning};
struct Output {template<LogLevel,class... T>static void send(T...) {}};
template<class T>const T& to_generic_string(const T& value){return value;}
namespace Unreal {
using UObject=TestObject;
struct Item {UObject* object{};int serial{};bool valid=true;
    UObject* GetUObject()const{return object;}bool IsValid(bool)const{return valid;}
    int GetSerialNumber()const{return serial;}
};
struct FUObjectArray {
    inline static std::array<Item,17> items{};
    static Item* IndexToObject(int i){return i>=0 && i<int(items.size())?&items[i]:nullptr;}
};
}}
#define STR(x) x
#include "trace_material_visit_layout.inl"
static std::array<TestObject*,2> trace_roots{};
template<class Visitor>bool VisitReplayObjectsOfClass(const wchar_t*,Visitor visit) {
    for(auto* root:trace_roots)if(!visit(root))return false;return true;
}
// Object enumeration/GC and native animation lookup are external services.
// This controlled lease checks only the objects production actually requests;
// it never adds an omitted MID/proxy or grants material ownership.
struct TestLease {
    struct Entry {TestObject* object;int index,serial;};std::vector<Entry> entries;
    Status Acquire(std::uintptr_t,const std::vector<void*>& owners,std::size_t) {
        for(auto* p:owners) {
            auto* object=static_cast<TestObject*>(p);const auto i=object->GetInternalIndex();
            const auto* item=RC::Unreal::FUObjectArray::IndexToObject(i);
            if(!item || item->object!=object || !item->valid)return {false};
            entries.push_back({object,i,item->serial});
        }
        return Validate();
    }
    Status Validate()const {
        for(const auto& entry:entries) {
            const auto* item=RC::Unreal::FUObjectArray::IndexToObject(entry.index);
            if(!item || item->object!=entry.object || !item->valid || item->serial!=entry.serial)return {false};
        }
        return {};
    }
    Status Release(){entries.clear();return {};}
    std::size_t owned_bytes()const{return sizeof(*this)+entries.capacity()*sizeof(Entry);}
};
// The generator copies production declarations, memory operations, complete
// Capture, ValidateBindings/Values, Prepare and Install without rewriting them.
struct TraceMaterialImage {
    using Sc6ReplayTraceState=TraceMaterialImage;
#include "trace_material_declarations.inl"
    std::vector<Image> values_,bindings_;
    std::array<DynamicImage,10> dynamic_{};std::size_t dynamic_count_{};
    TestLease lease_;std::uintptr_t base_{};void* world_{};DWORD thread_{};
    std::size_t budget_{};bool captured_{},validation_only_{};
    const TraceMaterialImage* survivor_source_{};
    template<class T>static T& At(void* p,std::size_t o){return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+o);}
    static Status Fail(const char* check){std::printf("trace admission rejected check=%s\n",check);return {false};}
    // This case contains a live fixed-pool controller, never expired weak rows.
    bool OwnsExpired(const Ref&)const{return false;}bool RetainExpired(const Ref&){return false;}
    bool ReleaseExpired(){return expired_.empty();}
#include "trace_material_methods.inl"
};
static TestObject* animation_result;
static TestObject* GetAnimation(void*){return animation_result;}
static void MakeWeak(void* out,const void* p) {
    const auto* object=static_cast<const TestObject*>(p);const int i=object->GetInternalIndex();
    const std::array<int,2> weak{i,RC::Unreal::FUObjectArray::items[i].serial};std::memcpy(out,weak.data(),sizeof(weak));
}
static bool MapService(std::uintptr_t base,std::size_t rva,void* function) {
    if(!VirtualAlloc(reinterpret_cast<void*>((base+rva)&~std::uintptr_t{4095}),4096,MEM_COMMIT,PAGE_EXECUTE_READWRITE))return false;
    unsigned char jump[]{0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};std::memcpy(jump+2,&function,8);
    std::memcpy(reinterpret_cast<void*>(base+rva),jump,sizeof(jump));
    return FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(base+rva),sizeof(jump))!=0;
}
enum class MaterialCase {
    Empty,Override,AssetFallback,NullOverrideFallback,SecondAssetSlot,NullSlots,
    NegativeOverrideCount,OverrideCountPastCapacity,OverrideOverBound,OverrideMissingStorage,
    OverrideStrayStorage,UnreadableOverrides,NegativeAssetCount,AssetCountPastCapacity,
    AssetOverBound,AssetMissingStorage,AssetStrayStorage,UnreadableSlots,UnreadableAsset,OverflowSlots
};
static int MaterialAdmission(std::uintptr_t base,MaterialCase selected) {
    using Image=TraceMaterialImage;
    std::array<TestObject,17> objects{};
    for(int i=0;i<int(objects.size());++i) {
        Image::At<int>(&objects[i],0xc)=i;RC::Unreal::FUObjectArray::items[i]={&objects[i],100+i,true};
    }
    auto* world=&objects[16];
    for(unsigned i=0;i<2;++i) {
        auto* component=&objects[4*i];auto* chara=&objects[4*i+1];
        auto* manager=&objects[4*i+2];auto* scene=&objects[4*i+3];trace_roots[i]=component;
        Image::At<std::uintptr_t>(component,0)=base+0x3360ca8;
        Image::At<void*>(component,0x1c8)=world;Image::At<unsigned>(component,0x498)=i;
        Image::At<TestObject*>(component,0x490)=chara;Image::At<TestObject*>(chara,0x458)=manager;
        Image::At<TestObject*>(manager,0x3a8)=component;Image::At<TestObject*>(chara,0x168)=scene;
        Image::At<TestObject*>(scene,0x190)=chara;
    }
    auto* actor=&objects[8];auto* mesh=&objects[9];animation_result=&objects[10];
    auto* asset=&objects[11];auto* mid=&objects[12];auto* parent=&objects[13];
    alignas(16) std::array<std::byte,0x108> controller{};
    Image::At<std::uintptr_t>(controller.data(),0)=base+0x3362590;
    Image::At<int>(controller.data(),8)=1;Image::At<int>(controller.data(),12)=1;
    Image::Ref ref{controller.data()+16,controller.data()};
    Image::At<TestObject*>(ref.state,8)=actor;
    Image::At<Image::Array>(trace_roots[0],0x418)={reinterpret_cast<std::byte*>(&ref),1,1};
    Image::At<std::uintptr_t>(actor,0)=base+0x3361660;Image::At<std::uintptr_t>(mesh,0)=base+0x38829c0;
    Image::At<TestObject*>(actor,0x168)=mesh;Image::At<TestObject*>(actor,0x398)=mesh;
    Image::At<TestObject*>(mesh,0x190)=actor;Image::At<TestObject*>(mesh,0x910)=asset;
    alignas(16) std::array<std::byte,0xb0> animation_proxy{};
    Image::At<void*>(animation_result,0x350)=animation_proxy.data();
    // Native provider: asset count +A8, nonnull override mesh+808 first.
    // The recognized MID layout is evidence of a writer, not its ownership.
    const bool unsupported=selected!=MaterialCase::Empty && selected!=MaterialCase::NullSlots;
    std::array<TestObject*,1> overrides{};
    alignas(8) std::array<std::byte,0x60> asset_slot{};
    // A native TArray may retain capacity with count zero. Both slot count and
    // capacity must describe real readable backing; only live slots are used.
    Image::At<Image::Array>(asset,0xa0)={asset_slot.data(),0,2};
    auto& override_header=Image::At<Image::Array>(mesh,0x808);
    auto& asset_header=Image::At<Image::Array>(asset,0xa0);
    auto* unreadable=reinterpret_cast<std::byte*>(base+0x1000); // Reserved PAGE_NOACCESS.
    switch(selected) {
    case MaterialCase::Empty:break;
    case MaterialCase::Override:
        asset_header.count=1;Image::At<TestObject*>(asset_slot.data(),0)=parent;
        overrides[0]=mid;override_header={reinterpret_cast<std::byte*>(overrides.data()),1,1};break;
    case MaterialCase::AssetFallback:
        asset_header.count=1;Image::At<TestObject*>(asset_slot.data(),0)=mid;break;
    case MaterialCase::NullOverrideFallback:
        asset_header.count=1;Image::At<TestObject*>(asset_slot.data(),0)=mid;
        override_header={reinterpret_cast<std::byte*>(overrides.data()),1,1};break;
    case MaterialCase::SecondAssetSlot:
        asset_header.count=2;Image::At<TestObject*>(asset_slot.data(),0x30)=mid;
        override_header={reinterpret_cast<std::byte*>(overrides.data()),1,1};break;
    case MaterialCase::NullSlots:
        asset_header.count=2;override_header={reinterpret_cast<std::byte*>(overrides.data()),1,1};break;
    case MaterialCase::NegativeOverrideCount:override_header.count=-1;break;
    case MaterialCase::OverrideCountPastCapacity:override_header.count=1;break;
    case MaterialCase::OverrideOverBound:override_header={reinterpret_cast<std::byte*>(overrides.data()),0,257};break;
    case MaterialCase::OverrideMissingStorage:override_header={nullptr,0,1};break;
    case MaterialCase::OverrideStrayStorage:override_header.data=reinterpret_cast<std::byte*>(overrides.data());break;
    case MaterialCase::UnreadableOverrides:override_header={unreadable,1,1};break;
    case MaterialCase::NegativeAssetCount:asset_header.count=-1;break;
    case MaterialCase::AssetCountPastCapacity:asset_header.count=3;break;
    case MaterialCase::AssetOverBound:asset_header.capacity=257;break;
    case MaterialCase::AssetMissingStorage:asset_header.data=nullptr;break;
    case MaterialCase::AssetStrayStorage:asset_header.capacity=0;break;
    case MaterialCase::UnreadableSlots:asset_header={unreadable,1,1};break;
    case MaterialCase::UnreadableAsset:Image::At<void*>(mesh,0x910)=unreadable;break;
    case MaterialCase::OverflowSlots:asset_header={reinterpret_cast<std::byte*>(UINTPTR_MAX-7),1,1};break;
    }
    Image::At<std::uintptr_t>(mid,0)=base+0x391ee70;
    Image::At<TestObject*>(mid,0x20)=world;Image::At<TestObject*>(mid,0x78)=parent;
    alignas(8) std::array<std::byte,0x28> vector_row{};
    Image::At<std::uint64_t>(vector_row.data(),0)=0x100000015ull;
    Image::At<Image::Array>(mid,0xb8)={vector_row.data(),1,1};
    // No fake queued task or fabricated completion flag: these raw addresses
    // deliberately carry no material-proxy lifetime/completion proof.
    std::array<std::array<std::byte,0x148>,3> proxies{};
    for(unsigned i=0;i<3;++i)Image::At<void*>(mid,0xf0+i*8)=proxies[i].data();
    const auto write_color=[&](float value){Image::At<std::array<float,4>>(vector_row.data(),8)={value,0.25f,0.5f,1.0f};};
    write_color(0.125f);Image::At<unsigned>(ref.state,0xcc)=101;
    Image a,b;
    if(!a.Capture(base,world,4*1024*1024).ok())return 80;
    // Independently authored B, before any A publication. Only the existing
    // row's color changes: FName/GUID, pointer/count/capacity stay unchanged.
    write_color(0.75f);Image::At<unsigned>(ref.state,0xcc)=202;
    if(!b.Capture(base,world,4*1024*1024).ok())return 81;
    const auto b_objects=objects;const auto b_controller=controller;const auto b_row=vector_row;
    const auto b_proxies=proxies;const auto b_overrides=overrides;const auto b_asset_slot=asset_slot;
    const auto b_lease_count=b.lease_.entries.size();
    const auto unchanged=[&]{
        for(std::size_t i=0;i<objects.size();++i)if(objects[i].bytes!=b_objects[i].bytes)return false;
        return controller==b_controller && vector_row==b_row && proxies==b_proxies
            && overrides==b_overrides && asset_slot==b_asset_slot
            && b.captured_ && b.lease_.entries.size()==b_lease_count && b.ValidateValues().ok();
    };
    // A real binding rejection control, not a supplied admission predicate.
    auto* saved_asset=Image::At<void*>(mesh,0x910);Image::At<void*>(mesh,0x910)=nullptr;
    const bool rejected_changed_asset=!a.Prepare(b).ok();Image::At<void*>(mesh,0x910)=saved_asset;
    if(!rejected_changed_asset || !unchanged())return 82;
    const auto admitted=a.Prepare(b);
    if(!unchanged())return 83;
    if(unsupported) {
        if(admitted.ok()) {
            std::printf("G1 RED: trace publication admitted unsupported material provider case=%u before A publication\n",unsigned(selected));
            std::puts("B checked unchanged; no setter, vector/refresh task, particle marker or Install executed");
            return 125; // First unsupported boundary; never continue into effects.
        }
        if(selected==MaterialCase::Override)std::puts("unsupported trace MID rejected before A publication; B unchanged");
        return 0;
    }
    if(!admitted.ok() || !a.Install().ok() || Image::At<unsigned>(ref.state,0xcc)!=101
        || !b.Install().ok() || !unchanged())return 84;
    if(selected==MaterialCase::Empty)std::puts("material-free production capture/Prepare/Install control passed");
    return 0;
}
int main() {
    auto* memory=VirtualAlloc(nullptr,0x2000000,MEM_RESERVE,PAGE_NOACCESS);if(!memory)return 85;
    const auto base=reinterpret_cast<std::uintptr_t>(memory);
    if(!MapService(base,0xf7bad0,reinterpret_cast<void*>(&MakeWeak))
        || !MapService(base,0x1d9d460,reinterpret_cast<void*>(&GetAnimation))) {VirtualFree(memory,0,MEM_RELEASE);return 86;}
    auto result=MaterialAdmission(base,MaterialCase::Empty);
    if(!result)result=MaterialAdmission(base,MaterialCase::Override);
    for(unsigned i=unsigned(MaterialCase::AssetFallback);!result && i<=unsigned(MaterialCase::OverflowSlots);++i) {
        result=MaterialAdmission(base,MaterialCase(i));
        if(result)std::printf("trace material provider control failed case=%u exit=%d\n",i,result);
    }
    if(!result)std::puts("trace material fallback, null slots and malformed storage controls passed");
    VirtualFree(memory,0,MEM_RELEASE);return result;
}
#endif
