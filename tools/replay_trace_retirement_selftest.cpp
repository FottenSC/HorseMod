// Compiled by the focused pytest with the current production method extracted
// verbatim. Native callees/GC are controlled fixtures, not native-game proof.
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <vector>
#include <set>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <algorithm>

enum class FailureCode { CapacityExceeded,GenerationMismatch,RestoreVerificationFailed,IllegalTransition };
struct Status {bool good{true};bool ok() const {return good;}static Status success() {return {};}static Status failure(FailureCode) {return {false};}};
namespace RC {enum class LogLevel {Default,Warning};struct Output {template<LogLevel,class... T>static void send(T...) {}};}
#define STR(x) x

struct Object { std::array<std::byte,0x1000> bytes{};bool live{true};Object* mesh{};std::vector<Object*> members; };
struct Id {Object* object{};friend bool operator==(const Id&,const Id&)=default;};
struct Ref {std::byte* state{};std::byte* controller{};};
struct State {Ref ref;Id actor,attachment,mesh,animation;};
struct Array {std::byte* data{};int count{},capacity{};};
struct Root {Id component,manager,chara,scene;};
struct Image {std::byte* address{};std::vector<std::byte> bytes;unsigned mutable_bits{};};
struct DynamicImage {
    enum class Kind { Map,WeakReferences,ChildStrongReferences };
    std::byte* address{};std::array<std::byte,0x50> header{};std::vector<std::byte> bytes;Kind kind{};
    bool weak() const {return kind==Kind::WeakReferences;}
    std::size_t header_size() const {return kind==Kind::Map?0x50:16;}
};
static std::set<void*> dead;
template<class T>T& At(void* p,std::size_t offset) {
    assert(!dead.contains(p));return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+offset);
}
static bool Live(const Id& id) {return !id.object || id.object->live;}
static int calls{},fault{},membership_fault{};
static void Changed() {if(++calls==fault)RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);}
static int RemoveWeak(void* storage,Ref** target) {
    assert(At<int>((*target)->controller,8)>0);
    auto& a=At<Array>(storage,0);int out{},removed{};
    for(int i=0;i<a.count;++i) {
        auto row=At<Ref>(a.data,std::size_t(i)*16);
        if(row.state==(*target)->state) {--At<int>(row.controller,12);++removed;}
        else At<Ref>(a.data,std::size_t(out++)*16)=row;
    }
    a.count=out;Changed();return removed;
}
static bool DestroyActor(void* pointer,bool force,bool modify) {
    assert(!force && modify);auto* object=static_cast<Object*>(pointer);assert(object->live);
    object->live=false;object->mesh->live=false;At<unsigned>(object->mesh,0x188)=0;
    Changed();return true;
}
static void DestroyAttachment(void* pointer,bool promote) {
    assert(!promote);auto* object=static_cast<Object*>(pointer);assert(object->live);
    auto* owner=At<Object*>(object,0x190);auto& members=owner->members;
    members.erase(std::remove(members.begin(),members.end(),object),members.end());
    if(membership_fault==1)members.clear();
    if(membership_fault==2)members.push_back(object);
    object->live=false;At<unsigned>(object,0x188)=0;Changed();
}
static int RemoveStrong(void* storage,std::byte*** target) {
    auto& a=At<Array>(storage,0);int out{},removed{};
    for(int i=0;i<a.count;++i) {
        auto row=At<Ref>(a.data,std::size_t(i)*16);
        if(row.state==**target) {
            assert(At<int>(row.controller,8)==1 && At<int>(row.controller,12)==1);
            // Verified native1408CED30 releases the child's cached parent weak
            // reference before the control block's implicit weak is released.
            const auto parent=At<Ref>(row.state,0xa0);
            if(parent.controller) {assert(At<int>(parent.controller,12)>1);--At<int>(parent.controller,12);}
            At<int>(row.controller,8)=At<int>(row.controller,12)=0;dead.insert(row.state);++removed;
        } else At<Ref>(a.data,std::size_t(out++)*16)=row;
    }
    a.count=out;Changed();return removed;
}
struct Fixture {
    using Sc6ReplayTraceState=Fixture;
    std::uintptr_t base_{};
    void* world_{};
    std::vector<Root> roots_;
    std::vector<State> states_;
    std::vector<Image> values_;
    std::array<DynamicImage,10> dynamic_{};std::size_t dynamic_count_{};
    bool native_owner{true};
    bool bindings_valid{true},values_valid{true},lifecycle_valid{true};
    bool OwnsNativeChild(const Ref& ref) const {return native_owner && At<int>(ref.controller,8)==1;}
    static Status Fail(const char*) {return {false};}
    Status ValidateBindings() const {return {bindings_valid};}
    Status ValidateValues() const {return {bindings_valid && values_valid};}
    Status CheckRetirementWorld() const {return {lifecycle_valid};}
    Status CheckChild(const State& state,const Fixture&,const Fixture&) const {return {lifecycle_valid && OwnsNativeChild(state.ref)};}
    Status Prepare(const Fixture&) const {return {false};} // No unchanged-image admission is granted by this fixture.
    static bool Writable(const Image& image) {return image.address && !image.bytes.empty();}
    static Id Identify(Object* object) {return {object && object->live?object:nullptr};}
    template<class Accept>static bool VisitOwnedComponents(Object* actor,Accept accept) {
        for(auto* object:actor->members)if(!accept(object))return false;return true;
    }
    // The generator copies both the journal declaration and method unchanged.
#include "replay_trace_retirement_method.inl"
};
static void Jump(std::uintptr_t base,std::size_t offset,void* function) {
    assert(VirtualAlloc(reinterpret_cast<void*>((base+offset)&~std::uintptr_t{4095}),4096,MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    unsigned char code[]{0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
    std::memcpy(code+2,&function,8);std::memcpy(reinterpret_cast<void*>(base+offset),code,sizeof(code));
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(base+offset),sizeof(code));
}
int main() {
    auto memory=VirtualAlloc(nullptr,0x2140000,MEM_RESERVE,PAGE_NOACCESS);assert(memory);
    const auto base=reinterpret_cast<std::uintptr_t>(memory);
    Jump(base,0x213cc20,reinterpret_cast<void*>(&RemoveWeak));Jump(base,0x1c11c60,reinterpret_cast<void*>(&DestroyActor));
    Jump(base,0x1d99730,reinterpret_cast<void*>(&DestroyAttachment));Jump(base,0x1d0a460,reinterpret_cast<void*>(&RemoveStrong));
    {
        std::array<Object,6> objects{};
        std::array<std::array<std::byte,0x108>,2> controllers{};
        std::array<std::byte,32> values{};
        Fixture a,b;a.base_=b.base_=base;a.world_=b.world_=&objects[0];
        a.roots_=b.roots_={{{&objects[0]},{&objects[1]},{&objects[2]}}};
        for(auto& c:controllers)At<int>(c.data(),8)=1;
        State retained{{controllers[0].data()+16,controllers[0].data()},{&objects[3]},{},{},{}};
        State child{{controllers[1].data()+16,controllers[1].data()},{&objects[4]},{},{},{}};
        a.states_={retained};b.states_={retained,child};
        a.values_={{values.data(),std::vector<std::byte>(4)}};
        b.values_=a.values_;b.values_.push_back({values.data()+8,std::vector<std::byte>(4)});
        a.dynamic_count_=b.dynamic_count_=1;
        a.dynamic_[0].kind=b.dynamic_[0].kind=DynamicImage::Kind::ChildStrongReferences;
        a.dynamic_[0].address=b.dynamic_[0].address=reinterpret_cast<std::byte*>(&objects[0])+0x428;
        // A's historical backing may alias B's current backing. It is copied
        // during publication, never treated as a privately owned allocation.
        At<Array>(a.dynamic_[0].header.data(),0)={values.data()+16,0,1};
        At<Array>(b.dynamic_[0].header.data(),0)={values.data()+16,1,1};
        assert(a.PrepareHistoricalChildren(b).ok());
        b.bindings_valid=false;b.values_valid=false;assert(!a.PrepareHistoricalChildren(b).ok());
        b.bindings_valid=b.values_valid=true;
        b.lifecycle_valid=false;assert(!a.PrepareHistoricalChildren(b).ok());b.lifecycle_valid=true;
        b.native_owner=false;assert(!a.PrepareHistoricalChildren(b).ok());b.native_owner=true;
        b.roots_[0].manager={};assert(!a.PrepareHistoricalChildren(b).ok());b.roots_=a.roots_;
        b.states_[0].actor={};assert(!a.PrepareHistoricalChildren(b).ok());b.states_[0]=retained;
        b.values_[0].bytes.resize(3);assert(!a.PrepareHistoricalChildren(b).ok());b.values_[0].bytes.resize(4);
        b.values_[1].address=values.data()+1;assert(!a.PrepareHistoricalChildren(b).ok());b.values_[1].address=values.data()+8;
        At<int>(a.dynamic_[0].header.data(),8)=1;assert(!a.PrepareHistoricalChildren(b).ok());At<int>(a.dynamic_[0].header.data(),8)=0;
        At<int>(b.dynamic_[0].header.data(),8)=2;assert(!a.PrepareHistoricalChildren(b).ok());At<int>(b.dynamic_[0].header.data(),8)=1;
        assert(a.PrepareHistoricalChildren(b).ok());
    }
    for(const bool private_storage:{false,true})for(int failing=0;failing<=19;++failing) {
        calls=0;fault=failing<=17?failing:0;membership_fault=failing>17?failing-17:0;dead.clear();
        std::array<Object,11> objects{};
        std::array<std::array<std::byte,0x108>,2> controls{};
        std::array<std::array<Ref,4>,8> arrays{};
        std::array<Array,8> displaced_headers{};
        std::array<std::array<Ref,4>,8> published_arrays{};
        std::array<std::byte,72> protected_b{};protected_b.fill(std::byte{0x5a});
        Ref b{protected_b.data()+16,protected_b.data()};At<int>(b.controller,8)=2;At<int>(b.controller,12)=7;
        const auto saved_b=protected_b;std::array<Ref,2> base_refs{b,b};
        Fixture f;f.base_=base;f.roots_={{{&objects[0]},{&objects[7]}},{{&objects[1]},{&objects[8]}}};
        objects[7].members={&objects[9]};objects[8].members={&objects[10],&objects[6]};
        At<Object*>(&objects[9],0x190)=&objects[7];At<Object*>(&objects[10],0x190)=&objects[8];
        At<Object*>(&objects[6],0x190)=&objects[8];
        Fixture::Retirement p;p.count=2;p.step=Fixture::Retirement::Step::Ready;
        for(unsigned i=0;i<2;++i) {
            Ref ref{controls[i].data()+16,controls[i].data()};At<int>(ref.controller,8)=1;At<int>(ref.controller,12)=7;
            At<std::uintptr_t>(ref.controller,0)=base+0x3362590;
            if(private_storage)At<Ref>(ref.state,0xa0)=b;
            auto* actor=&objects[2+i*2];auto* mesh=&objects[3+i*2];actor->mesh=mesh;At<unsigned>(mesh,0x188)=3;
            p.children[i]={ref,{actor},{i?&objects[6]:nullptr},{mesh},{}};p.roots[i]=&objects[i];
            arrays[i*4][0]=ref;At<Array>(&objects[i],0x428)={reinterpret_cast<std::byte*>(arrays[i*4].data()),1,4};
            At<Array>(&objects[i],0x418)={reinterpret_cast<std::byte*>(&base_refs[i]),1,1};
            unsigned j{};for(auto offset:{0x3e0u,0x3f0u,0x400u}) {
                auto& rows=arrays[i*4+1+j++];rows[0]=rows[1]=ref;rows[2]={nullptr,nullptr};rows[3]=b;
                At<Array>(&objects[i],offset)={reinterpret_cast<std::byte*>(rows.data()),4,4};
            }
        }
        if(private_storage) {
            p.storage=Fixture::Retirement::Storage::Private;
            for(unsigned i=0;i<2;++i) {
                displaced_headers[i*4]=At<Array>(&objects[i],0x428);
                p.private_strong_headers[i]=&displaced_headers[i*4];
                published_arrays[i*4][0]=b;
                At<Array>(&objects[i],0x428)={reinterpret_cast<std::byte*>(published_arrays[i*4].data()),1,4};
                unsigned j{};
                for(auto offset:{0x3e0u,0x3f0u,0x400u}) {
                    const auto slot=i*4+1+j++;
                    displaced_headers[slot]=At<Array>(&objects[i],offset);
                    p.private_weak_headers[p.private_weak_count++]=&displaced_headers[slot];
                    published_arrays[slot][0]=b;
                    At<Array>(&objects[i],offset)={reinterpret_cast<std::byte*>(published_arrays[slot].data()),1,4};
                }
            }
        }
        if(!failing) {
            if(private_storage) {
                const auto saved=displaced_headers[1];
                displaced_headers[1]=At<Array>(&objects[0],0x3e0);
                assert(!f.RetireChildrenNative(p) && calls==0);
                displaced_headers[1]=saved;
                displaced_headers[1].data=At<Array>(&objects[0],0x3e0).data+16;
                assert(!f.RetireChildrenNative(p) && calls==0);
                displaced_headers[1]=saved;
                auto* header=p.private_weak_headers[1];p.private_weak_headers[1]=p.private_weak_headers[0];
                assert(!f.RetireChildrenNative(p) && calls==0);p.private_weak_headers[1]=header;
                At<int>(controls[0].data(),8)=2;
                assert(!f.RetireChildrenNative(p) && calls==0);At<int>(controls[0].data(),8)=1;
                At<std::uintptr_t>(controls[0].data(),0)=0;
                assert(!f.RetireChildrenNative(p) && calls==0);At<std::uintptr_t>(controls[0].data(),0)=base+0x3362590;
                published_arrays[0][0]=p.children[0].ref;
                assert(!f.RetireChildrenNative(p) && calls==0);published_arrays[0][0]=b;
                published_arrays[1][0]=p.children[0].ref;
                assert(!f.RetireChildrenNative(p) && calls==0);published_arrays[1][0]=b;
                displaced_headers[0].count=2;
                assert(!f.RetireChildrenNative(p) && calls==0);displaced_headers[0].count=1;
                assert(!f.RetireAddedForUndo(f,p).ok() && calls==0);
            } else assert(!f.RetirePrivateForCommit(f,p).ok() && calls==0);
            if(!private_storage) {f.native_owner=false;assert(!f.RetireChildrenNative(p) && calls==0 && p.step==Fixture::Retirement::Step::Ready);}
            f.native_owner=true;
        }
        const auto retire=[&] {return private_storage?f.RetirePrivateForCommit(f,p):f.RetireAddedForUndo(f,p);};
        const bool ok=retire().ok();
        auto expected_b=saved_b;
        if(private_storage)At<int>(expected_b.data(),12)-=static_cast<int>(dead.size());
        assert(protected_b==expected_b);
        if(!failing) {
            assert(ok && calls==17 && p.completed==2 && p.step==Fixture::Retirement::Step::Finished && dead.size()==2);
            assert(retire().ok() && calls==17);
            assert(objects[7].members==std::vector<Object*>{&objects[9]} && objects[8].members==std::vector<Object*>{&objects[10]});
            for(auto& root:f.roots_)for(auto offset:{0x3e0u,0x3f0u,0x400u})assert(At<Array>(root.component.object,offset).count==(private_storage?1:2));
            if(private_storage)for(unsigned i=0;i<2;++i) {
                assert(displaced_headers[i*4].count==0);
                for(unsigned j=1;j<4;++j)assert(displaced_headers[i*4+j].count==2);
                assert(At<Array>(&objects[i],0x428).count==1);
            }
        } else {
            assert(!ok && calls==(failing<=17?failing:17) && p.step==Fixture::Retirement::Step::Failed);
            const auto before=calls;assert(!retire().ok() && calls==before); // No uncertain call is repeated.
        }
    }
    VirtualFree(memory,0,MEM_RELEASE);
    std::puts("Production trace retirement method: success, expired-reference preservation, payload lifetime, 17 fault boundaries and no retry after uncertainty pass. Native callees and GC are fixtures.");
    return 0;
}
