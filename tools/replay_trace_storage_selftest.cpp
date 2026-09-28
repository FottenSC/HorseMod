// Production Prepared storage/publication/undo, using the existing retirement
// fixture's controlled UObject/native lifecycle domain. No engine execution is
// granted here. Native allocation ownership and reference counts are real data.
#define main retirement_fixture_main
#include "replay_trace_retirement_selftest.cpp"
#undef main
#include "ReplayTraceWeakReference.hpp"
#include "Sc6ReplayTraceChildFactory.hpp"
#include <span>
#include <cstdlib>
#include <climits>
#include <atomic>
#include <cstring>
#include "replay_qualification_mod/ReplayTraceMembership.hpp"
using Horse::Deterministic::ReplayTraceWeakController;
using Horse::Deterministic::ReplayTraceReopenControls;
using Horse::Deterministic::Sc6ReplayTraceChildFactory;
static std::set<void*> storage_allocations;
static unsigned allocate_calls{},fail_allocation{};
static std::size_t Quantize(std::size_t n,unsigned) {return (n+15)&~std::size_t{15};}
static void* Allocate(std::size_t n) {
    if(++allocate_calls==fail_allocation)return nullptr;
    auto* p=std::malloc(n);assert(p);assert(storage_allocations.insert(p).second);return p;
}
static void Free(void* p) {if(p) {assert(storage_allocations.erase(p)==1);std::free(p);}}
namespace Horse::GameImGui {struct PresentHook {static auto& instance(){static PresentHook value;return value;}bool pending{};bool replay_retirement_pending()const{return pending;}};}
namespace StorageTest {
struct ReplayGpuCompletion {
 enum class Result {Complete,Pending,TimedOut,DeviceFailure};
 bool done=true;Result value=Result::Complete;std::uint64_t serial=1;
 bool retired()const{return done;}Result result()const{return value;}std::uint64_t submitted_serial()const{return serial;}
};
struct Sc6ReplayVfxState {struct ParticleBirthSet {bool admitted{true};};};
struct Sc6ReplayTraceState : Fixture {
    class Prepared;
    std::vector<Image> bindings_;
    DWORD thread_{GetCurrentThreadId()};
    bool validation_only_{};
    const Sc6ReplayTraceState* survivor_source_{};
    static inline unsigned identity_deletions{};
    static void DeleteExpiredController(void* p) {assert(At<int>(p,12)==0);++identity_deletions;dead.insert(p);}
    struct FreshChild {
        enum class HistoryPhase {Ready};enum class AnimationPhase {Ready};enum class StrongPhase {Private,Publishing,Published};
        enum class ExecutionOutcome {None,Recovered,Committed};ExecutionOutcome execution_outcome=ExecutionOutcome::None;
        State state;Ref cached_parent{};Sc6ReplayTraceChildFactory::Journal factory;Array strong;
        bool prepared=true,retired=false,identity_retained=true;
        bool execution_settled{},native_dead{},adopted{};
        std::size_t allowance=123;
        HistoryPhase history_phase=HistoryPhase::Ready;AnimationPhase animation_phase=AnimationPhase::Ready;
        StrongPhase strong_phase=StrongPhase::Private;
        struct Lease {bool live=true,release_ok=true;unsigned releases{};Status Validate()const{return{live};}Status Release(){++releases;return{release_ok};}} lease;
    };
    Status PrepareStorage(const Sc6ReplayTraceState&,std::size_t,Prepared&,
        const Sc6ReplayVfxState::ParticleBirthSet* =nullptr,const Sc6ReplayTraceState* =nullptr,std::span<FreshChild* const> ={}) const;
    bool OwnsExpired(const Ref&) const {return false;}
    void DescribeOwnershipDelta(const Sc6ReplayTraceState&) const {}
    // These native execution dependencies explicitly reject: this experiment
    // proves held publication/undo, not C execution or scene retirement.
    Status PrepareAddedRetirement(const Sc6ReplayTraceState&,const Sc6ReplayTraceState&,
        const Sc6ReplayVfxState::ParticleBirthSet&,Retirement&,const Retirement* =nullptr) const {return {false};}
    Status PrepareHistoricalRetirement(const Sc6ReplayTraceState& target,
        const Sc6ReplayVfxState::ParticleBirthSet& domain,Retirement& p) const {
        if(!domain.admitted || !target.PrepareHistoricalChildren(*this).ok())return {false};
        for(const auto& s:states_)if(!target.Contains(s.ref)) {
            if(p.count==p.children.size() || !OwnsNativeChild(s.ref))return {false};
            p.children[p.count]=s;p.roots[p.count++]=roots_[0].component.object;
        }
        p.step=Retirement::Step::Ready;return {};
    }
    bool DynamicEqual(const DynamicImage& d) const {
        if(std::memcmp(d.address+8,d.header.data()+8,d.header_size()-8))return false;
        const auto* data=At<void*>(d.address,0);
        return d.bytes.empty() || (data && !std::memcmp(data,d.bytes.data(),d.bytes.size()));
    }
    Status ValidateValues() const {
        if(!ValidateBindings().ok())return {false};
        for(const auto& v:values_)if(!Equal(v))return {false};
        for(std::size_t i=0;i<dynamic_count_;++i)if(!DynamicEqual(dynamic_[i]))return {false};
        return {};
    }
#include "trace_storage_image_methods.inl"
};
#include "trace_storage_prepared.inl"
struct Sc6ReplayParticleCopy {
 enum class Phase {Installed,Released};
 struct Witness {Phase phase=Phase::Released;bool pending{},native_write_uncommitted{};}w;
 bool blocks{};bool blocks_resume()const{return blocks;}
 const auto& witness()const{return w;}
};
// Compose actual host routing with the production trace release/membership
// methods above. Only unrelated particle participants and GPU edges are mocks.
struct Sc6ReplayHost {
 struct HistoricalRestore {
  struct Execution {enum class Capture{Empty,Current};Capture capture=Capture::Current;Sc6ReplayTraceState traces;};
  Execution* execution{};
  struct Undo {Sc6ReplayTraceState* traces{};}undo;
  struct Participant {bool dirty{};bool published()const{return dirty;}bool render_published()const{return dirty;}bool released()const{return !dirty;}bool empty()const{return !dirty;}}gpu,cpu,manager,pools,scheduler,world,physics_markers,ground,fresh_scheduler_transfer;
  struct Witness{bool pending{};}witness;
  bool game_dirty{},physics_dirty{},source_dirty{},execution_dirty{},surface_dirty{},rendering_dirty{},hud_dirty{},traces_dirty{},vfx_handler_dirty{};
  struct Particle {
   bool retired{},adopted{},settled{},native_dead{},slots_installed{};
   struct Binding {std::uintptr_t target{};}binding;
   struct Lease {Status ValidateObject(void*)const{return {};}Status Release(){return {};}}lease;
   void* slots{};std::size_t slot_bytes{},native_allowance{};std::vector<int> gpus;
   struct Render {std::size_t charged_bytes{};}render;
  };
  std::vector<Particle*> fresh_particles;
  std::vector<Sc6ReplayTraceState::FreshChild*> fresh_trace_children;
  bool native_retired=true;
 }operation;
 HistoricalRestore* historical_restore_=&operation;
 Sc6ReplayParticleCopy copy;Sc6ReplayParticleCopy* particle_copy_=&copy;
 std::atomic<bool> particle_command_pending_{};
 bool checkpoint_restoring_{};void* surface_event_{};
 struct Capture{bool PendingEnclosingWind()const{return false;}};Capture* checkpoint_capture_{};
 bool HistoricalNativeOwnersReleased() const noexcept;
 Status ReleaseFreshParticleOwnership(bool commit);
};
#include "trace_storage_host_release.inl"
#include "trace_storage_host_can_release.inl"
}
// Actual scheduler exclusion verification across the production trace visitor.
// Scheduler container allocation/publication is outside this controlled fixture.
namespace SchedulerTest {
template<class T,class A>T Field(const A& a,std::size_t offset) {T x;std::memcpy(&x,a.data()+offset,sizeof(x));return x;}
template<class A,class T>void Store(A& a,std::size_t offset,T x) {std::memcpy(a.data()+offset,&x,sizeof(x));}
bool SameBytes(std::uintptr_t p,const void* bytes,std::size_t n) {return !n || (p && !std::memcmp(reinterpret_cast<void*>(p),bytes,n));}
struct Sc6ReplaySchedulerState {
    struct Owner {std::uintptr_t object;};
    struct Tick {std::uintptr_t address;Owner owner;std::array<std::byte,0x58> binding_layout{};std::vector<std::array<std::byte,16>> prerequisites;};
    struct RetainedPrimaryTicks {const void* context{};Status(*visit)(const void*,void*,bool(*)(void*,std::uintptr_t)){};};
    struct Birth {std::uintptr_t value;std::uintptr_t component() const {return value;}};
    struct Births {std::vector<Birth> rows;const auto& members() const {return rows;}};
    struct PreparedRestore {
        Births birth_;std::size_t excluded_count_{};std::array<std::uintptr_t,80> excluded_ticks_{},excluded_owners_{};
        RetainedPrimaryTicks private_owners_;const Sc6ReplaySchedulerState* previous_image_{};
        std::uintptr_t base_{};std::uint64_t original_epoch_{},epoch_{};bool executing_{};
        Status ValidatePrivateOwners() const noexcept;
    };
    std::uintptr_t base_{};std::uint64_t epoch_{};std::vector<Tick> ticks_;
    bool IsLiveObject(const Owner& o) const {return o.object && reinterpret_cast<Object*>(o.object)->live;}
    Status ValidateExcludedTick(const PreparedRestore&,bool) const noexcept;
};
#include "trace_storage_scheduler_methods.inl"
}

static void TestMembershipTopology(){
    using R=ReplayTraceMembership::Ref;
    const auto observe=[](std::uintptr_t relocation,unsigned fault){
        ReplayTraceMembership t;const R a{relocation+0x110,relocation+0x100},b{relocation+0x210,relocation+0x200},parent{relocation+0x310,relocation+0x300};
        for(unsigned collection=0;collection<5;++collection){
            assert(t.Begin(collection,2));
            assert(t.Member(a,true,parent));
            assert(t.Member(fault==1?a:b,fault!=2,fault==2?R{}:fault==3?b:parent));
        }
        assert(t.complete());return t.hash();
    };
    assert(observe(0x1000,0)==observe(0x2000,0));
    for(unsigned fault=1;fault<4;++fault)assert(observe(0x1000,0)!=observe(0x1000,fault));
    ReplayTraceMembership t;assert(!t.complete()&&!t.Begin(1,0));assert(t.Begin(0,1));
    assert(!t.Member({0x120,0x100},true,{})&&!t.Begin(1,0));
    assert(t.Member({0x110,0x100},false,{}));assert(!t.Member({0x210,0x200},true,{}));
}
int main(int argc,char** argv) {
    if(argc>1 && std::strcmp(argv[1],"retirement")==0)return retirement_fixture_main();
    TestMembershipTopology();
    using Trace=StorageTest::Sc6ReplayTraceState;
    auto memory=VirtualAlloc(nullptr,0x2140000,MEM_RESERVE,PAGE_NOACCESS);assert(memory);
    const auto base=reinterpret_cast<std::uintptr_t>(memory);
    Jump(base,0xd50dc0,reinterpret_cast<void*>(&Quantize));Jump(base,0x4a61c0,reinterpret_cast<void*>(&Allocate));
    Jump(base,0xd46a00,reinterpret_cast<void*>(&Free));
    Jump(base,0x213cc20,reinterpret_cast<void*>(&RemoveWeak));Jump(base,0x1c11c60,reinterpret_cast<void*>(&DestroyActor));
    Jump(base,0x1d99730,reinterpret_cast<void*>(&DestroyAttachment));Jump(base,0x1d0a460,reinterpret_cast<void*>(&RemoveStrong));
    {
        // A settled live C child at the final release boundary. B excludes it.
        // This exercises real host routing and actual strong-array membership,
        // not a claim that this fixture simulates native construction or ticks.
        Object root,actor,mesh;std::array<std::byte,0x108> control{};
        Trace b; b.base_=base;b.roots_={{{&root},{},{}}};
        Trace::FreshChild child;child.state.ref={control.data()+16,control.data()};
        child.state.actor={&actor};child.state.mesh={&mesh};
        At<std::uintptr_t>(control.data(),0)=base+0x3362590;
        At<int>(control.data(),8)=1;At<int>(control.data(),12)=2;
        Ref member=child.state.ref;At<Array>(&root,0x428)={reinterpret_cast<std::byte*>(&member),1,1};
        child.strong.count=0;child.strong_phase=Trace::FreshChild::StrongPhase::Published;
        child.execution_settled=true;child.execution_outcome=Trace::FreshChild::ExecutionOutcome::Committed;
        StorageTest::Sc6ReplayHost h;
        StorageTest::Sc6ReplayHost::HistoricalRestore::Execution c;c.traces=b;c.traces.states_.push_back(child.state);
        h.operation.execution=&c;h.operation.undo.traces=&b;h.operation.fresh_trace_children={&child};
        assert(!b.Contains(member)&&c.traces.OwnsNativeChild(member));
        h.copy.w.pending=true;assert(!h.ReleaseFreshParticleOwnership(true).ok()&&child.identity_retained);h.copy.w.pending=false;
        h.operation.native_retired=false;assert(!h.ReleaseFreshParticleOwnership(true).ok());h.operation.native_retired=true;
        assert(!h.ReleaseFreshParticleOwnership(false).ok()&&child.identity_retained);
        At<Array>(&root,0x428).count=0;assert(!h.ReleaseFreshParticleOwnership(true).ok()&&child.identity_retained);
        At<Array>(&root,0x428).count=1;
        assert(!h.HistoricalNativeOwnersReleased());
        child.lease.release_ok=false;
        assert(!h.ReleaseFreshParticleOwnership(true).ok()&&!child.identity_retained&&!child.adopted);
        assert(At<int>(control.data(),12)==1&&At<int>(control.data(),8)==1&&!h.HistoricalNativeOwnersReleased());
        child.lease.release_ok=true;assert(h.ReleaseFreshParticleOwnership(true).ok()&&child.adopted&&!child.retired);
        assert(h.ReleaseFreshParticleOwnership(true).ok()&&child.lease.releases==2&&!b.Contains(member));
        assert(h.HistoricalNativeOwnersReleased());
        h.copy.w.native_write_uncommitted=true;assert(!h.HistoricalNativeOwnersReleased());h.copy.w.native_write_uncommitted=false;
        h.operation.world.dirty=true;assert(!h.HistoricalNativeOwnersReleased());h.operation.world.dirty=false;
        Horse::GameImGui::PresentHook::instance().pending=true;assert(!h.HistoricalNativeOwnersReleased());Horse::GameImGui::PresentHook::instance().pending=false;
        h.operation.fresh_trace_children.push_back(nullptr);assert(!h.HistoricalNativeOwnersReleased());h.operation.fresh_trace_children.pop_back();
        assert(h.HistoricalNativeOwnersReleased()&&c.traces.OwnsNativeChild(member)&&!b.Contains(member));
    }
    for(unsigned trial=0;trial<9;++trial) {
        assert(storage_allocations.empty());allocate_calls=fail_allocation=0;dead.clear();calls=fault=membership_fault=0;
        std::array<Object,10> objects{};std::array<std::array<std::byte,0x108>,3> controls{};
        unsigned common=2,child_value=55;
        Ref parent{controls[0].data()+16,controls[0].data()},child{controls[1].data()+16,controls[1].data()};
        for(auto& c:controls) {At<std::uintptr_t>(c.data(),0)=base+0x3362590;At<int>(c.data(),8)=1;}
        At<int>(parent.controller,12)=4;At<int>(child.controller,12)=2;
        Trace a,b;a.base_=b.base_=base;a.world_=b.world_=&objects[0];
        a.roots_=b.roots_={{{&objects[0]},{&objects[1]},{}},{{&objects[2]},{&objects[3]},{}}};
        State retained{parent,{&objects[4]},{},{&objects[9]},{}};
        State born{child,{&objects[5]},{},{&objects[6]},{}};
        for(auto index:{6,8,9})At<std::uintptr_t>(&objects[index],0)=base+0x38829c0;
        alignas(8) std::array<std::byte,0xb0> private_material_asset{},fresh_material_asset{};
        alignas(8) std::array<std::byte,0x30> private_material_slot{},fresh_material_slot{};
        At<Array>(private_material_asset.data(),0xa0)={private_material_slot.data(),1,1};
        At<Array>(fresh_material_asset.data(),0xa0)={fresh_material_slot.data(),1,1};
        At<void*>(&objects[6],0x910)=private_material_asset.data();
        At<void*>(&objects[8],0x910)=fresh_material_asset.data();
        objects[5].mesh=&objects[6];At<unsigned>(&objects[6],0x188)=3;
        At<Ref>(child.state,0xa0)=parent;
        a.states_={retained};b.states_={retained,born};
        a.values_={{reinterpret_cast<std::byte*>(&common),std::vector<std::byte>(4)}};
        At<unsigned>(a.values_[0].bytes.data(),0)=1;
        b.values_=a.values_;At<unsigned>(b.values_[0].bytes.data(),0)=2;
        b.values_.push_back({reinterpret_cast<std::byte*>(&child_value),std::vector<std::byte>(4)});
        At<unsigned>(b.values_.back().bytes.data(),0)=55;
        for(unsigned root=0;root<2;++root)for(unsigned slot=0;slot<5;++slot) {
            const auto i=a.dynamic_count_++;++b.dynamic_count_;
            auto& x=a.dynamic_[i];auto& y=b.dynamic_[i];
            constexpr unsigned offsets[]{0x500,0x428,0x3e0,0x3f0,0x400};
            x.address=y.address=reinterpret_cast<std::byte*>(a.roots_[root].component.object)+offsets[slot];
            x.kind=y.kind=slot==0?DynamicImage::Kind::Map:slot==1?DynamicImage::Kind::ChildStrongReferences:DynamicImage::Kind::WeakReferences;
            if(root==0 && slot>=1 && slot<=3) {
                auto* p=static_cast<std::byte*>(Allocate(64));std::memset(p,0,64);
                At<Ref>(p,0)=slot==1?child:parent;
                if(slot==2)At<Ref>(p,16)=child;
                At<Array>(y.header.data(),0)={p,slot==2?2:1,4};y.bytes.assign(p,p+64);
                if(slot!=1) {x.header=y.header;At<int>(x.header.data(),8)=1;x.bytes=y.bytes;
                    std::memset(x.bytes.data()+16,0,48);}
            }
            std::memcpy(y.address,y.header.data(),y.header_size());
        }
        assert(b.ValidateValues().ok());const auto original_allocations=storage_allocations;
        StorageTest::Sc6ReplayVfxState::ParticleBirthSet owners;
        if(trial>=5) {
            // The common view construction/lease is tested independently. This
            // case exercises the actual strong/weak/storage handoff with real
            // native-layout arrays and controlled allocator/lifecycle services.
            Trace survivor=a;survivor.validation_only_=true;survivor.survivor_source_=&a;
            Trace::FreshChild fresh;
            fresh.factory.phase=Sc6ReplayTraceChildFactory::Phase::Returned;
            fresh.factory.reference={controls[2].data()+16,controls[2].data()};
            fresh.state={{fresh.factory.reference.state,fresh.factory.reference.controller},{&objects[7]},{},{&objects[8]},{}};
            fresh.strong={reinterpret_cast<std::byte*>(&fresh.factory.reference),1,1};
            At<int>(fresh.state.ref.controller,12)=2;
            a.states_.push_back(fresh.state);
            auto* fields=static_cast<unsigned*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(fields);*fields=77;
            a.values_.push_back({reinterpret_cast<std::byte*>(fields),std::vector<std::byte>(4)});
            At<unsigned>(a.values_.back().bytes.data(),0)=77;
            auto& strong=a.dynamic_[1];strong.bytes.resize(16);At<Ref>(strong.bytes.data(),0)=fresh.state.ref;
            At<Array>(strong.header.data(),0)={reinterpret_cast<std::byte*>(&fresh.factory.reference),1,1};
            At<int>(a.dynamic_[2].header.data(),8)=2;At<Ref>(a.dynamic_[2].bytes.data(),16)=fresh.state.ref;
            std::array<Trace::FreshChild*,1> children{&fresh};
            {
                // The projected child is absent from historical_a_. Check its
                // real mesh too, before allocator/refcount publication effects.
                Trace::Prepared denied;
                const auto before_controls=controls;const auto before_allocations=storage_allocations;
                At<Object*>(fresh_material_slot.data(),0)=&objects[4];
                assert(!a.PrepareStorage(b,1<<20,denied,&owners,&survivor,children).ok());
                assert(controls==before_controls && storage_allocations==before_allocations
                    && fresh.strong.count==1 && b.ValidateValues().ok());
                At<Object*>(fresh_material_slot.data(),0)=nullptr;
            }
            {
                Trace::Prepared denied;
                At<Ref>(fresh.state.ref.state,0xa0)=parent;
                assert(!a.PrepareStorage(b,1<<20,denied,&owners,&survivor,children).ok());
                At<Ref>(fresh.state.ref.state,0xa0)={};
            }
            {
                Trace::Prepared denied;
                fresh.cached_parent=child;At<Ref>(fresh.state.ref.state,0xa0)=child;
                assert(!a.PrepareStorage(b,1<<20,denied,&owners,&survivor,children).ok());
                fresh.cached_parent={};At<Ref>(fresh.state.ref.state,0xa0)={};
            }
            {
                Trace::Prepared denied;fresh.lease.live=false;
                assert(!a.PrepareStorage(b,1<<20,denied,&owners,&survivor,children).ok()&&fresh.strong.count==1);
                fresh.lease.live=true;
            }
            {
                Trace::Prepared p;
                if(trial>=7) {
                    fresh.cached_parent=parent;At<Ref>(fresh.state.ref.state,0xa0)=parent;
                    ++At<int>(parent.controller,12); // History's native cached-parent weak owner.
                }
                assert(a.PrepareStorage(b,1<<20,p,&owners,&survivor,children).ok());
                assert(fresh.strong.count==1&&At<int>(fresh.state.ref.controller,8)==1&&At<int>(fresh.state.ref.controller,12)==3);
                const auto prepared_controls=controls;const auto prepared_allocations=storage_allocations;
                At<Object*>(fresh_material_slot.data(),0)=&objects[4];
                assert(!p.Publish().ok() && fresh.strong.count==1
                    && fresh.strong_phase==Trace::FreshChild::StrongPhase::Private
                    && controls==prepared_controls && storage_allocations==prepared_allocations && b.ValidateValues().ok());
                At<Object*>(fresh_material_slot.data(),0)=nullptr;
                At<Ref>(fresh.state.ref.state,0xa0)=trial>=7?Ref{}:parent;
                assert(!p.Publish().ok()&&fresh.strong.count==1&&b.ValidateValues().ok());
                At<Ref>(fresh.state.ref.state,0xa0)=fresh.cached_parent;
                DWORD protection{};
                if(trial==6)assert(VirtualProtect(fields,4096,PAGE_READONLY,&protection));
                assert(p.Publish().ok()==(trial!=6));
                assert(fresh.strong.count==0&&At<int>(fresh.state.ref.controller,8)==1);
                assert(fresh.strong_phase==(trial!=6?Trace::FreshChild::StrongPhase::Published:Trace::FreshChild::StrongPhase::Publishing));
                assert(At<Ref>(At<Array>(&objects[0],0x428).data,0).controller==fresh.state.ref.controller);
                assert(!p.BeginExecution(1<<20).ok()&&!p.Commit().ok()); // C contract remains deliberately closed.
                if(trial>=7) {
                    StorageTest::ReplayGpuCompletion completion;
                    assert(p.BeginExecution(1<<20,&completion).ok());
                    // Controlled native death, with no reconstructed expected
                    // observations: remove actual A memberships and release
                    // the payload/implicit weak owners exactly once.
                    At<Array>(&objects[0],0x428).count=0;
                    At<Array>(&objects[0],0x3e0).count=1;
                    At<int>(fresh.state.ref.controller,8)=0;At<int>(fresh.state.ref.controller,12)=1;
                    --At<int>(parent.controller,12);
                    objects[7].live=false;objects[8].live=false;dead.insert(fresh.state.ref.state);fresh.lease.live=false;
                    Trace c=survivor;c.validation_only_=false;c.survivor_source_=nullptr;
                    for(std::size_t i=0;i<c.dynamic_count_;++i) {
                        auto& d=c.dynamic_[i];std::memcpy(d.header.data(),d.address,d.header_size());
                        const auto header=At<Array>(d.address,0);
                        if(header.capacity)d.bytes.assign(header.data,header.data+std::size_t(header.capacity)*16);
                    }
                    assert(!p.SettleExecution(c).ok()); // Stale completion.
                    ++completion.serial;completion.value=StorageTest::ReplayGpuCompletion::Result::TimedOut;
                    assert(!p.SettleExecution(c).ok());
                    completion.value=StorageTest::ReplayGpuCompletion::Result::Complete;
                    assert(p.SettleExecution(c).ok());
                    assert(p.ReopenExecutionForUndo(a).ok());
                    assert(!p.SettleExecution(c).ok()); // Undo requires a new ordered completion.
                    ++completion.serial;assert(p.SettleExecution(c).ok());
                    if(trial==7)assert(p.Undo().ok()&&p.Undo().ok()&&b.ValidateValues().ok());
                    else assert(p.Commit().ok()&&!objects[5].live&&!objects[6].live);
                    assert(At<int>(parent.controller,12)==(trial==7?4:3)&&At<int>(fresh.state.ref.controller,12)==1
                        &&At<int>(fresh.state.ref.controller,8)==0&&fresh.strong.count==0);
                    if(trial==7)assert(storage_allocations==original_allocations&&calls==0);
                    assert(fresh.execution_outcome==(trial==7?Trace::FreshChild::ExecutionOutcome::Recovered:Trace::FreshChild::ExecutionOutcome::Committed));
                    const auto deletes=Trace::identity_deletions;
                    StorageTest::Sc6ReplayHost host;
                    StorageTest::Sc6ReplayHost::HistoricalRestore::Execution observed;observed.traces=c;
                    host.operation.execution=&observed;host.operation.undo.traces=&b;host.operation.fresh_trace_children={&fresh};
                    assert(!b.ReleaseExecutedFreshChildOwnership(fresh,trial==7).ok()&&fresh.identity_retained);
                    fresh.lease.release_ok=false;
                    assert(!host.ReleaseFreshParticleOwnership(trial==8).ok()&&!fresh.identity_retained&&!fresh.retired);
                    assert(Trace::identity_deletions==deletes+1);
                    fresh.lease.release_ok=true;
                    assert(host.ReleaseFreshParticleOwnership(trial==8).ok()&&fresh.retired&&fresh.allowance==0);
                    assert(host.HistoricalNativeOwnersReleased());
                    assert(host.ReleaseFreshParticleOwnership(trial==8).ok()&&Trace::identity_deletions==deletes+1&&fresh.lease.releases==2);
                } else {
                assert(p.Undo().ok()&&p.Undo().ok()&&b.ValidateValues().ok());
                assert(fresh.strong.count==1&&fresh.strong_phase==Trace::FreshChild::StrongPhase::Private
                    &&At<int>(fresh.state.ref.controller,8)==1&&At<int>(fresh.state.ref.controller,12)==2);
                assert(storage_allocations==original_allocations&&common==2&&child_value==55&&calls==0);
                }
            }
            VirtualFree(fields,0,MEM_RELEASE);
        } else
        {
            Trace::Prepared p;
            assert(!a.PrepareStorage(b,1<<20,p).ok()); // No implicit ownership admission.
            a.validation_only_=true;
            assert(!a.Install().ok() && !a.PrepareStorage(b,1<<20,p,&owners).ok() && b.ValidateValues().ok());
            a.validation_only_=false;b.validation_only_=true;
            assert(!a.PrepareStorage(b,1<<20,p,&owners).ok() && b.ValidateValues().ok());b.validation_only_=false;
            {
                // B-only private children must also be checked on the route
                // that uses PrepareHistoricalChildren instead of Prepare.
                Trace::Prepared denied;
                const auto before_controls=controls;const auto before_allocations=storage_allocations;
                At<Object*>(private_material_slot.data(),0)=&objects[4];
                assert(!a.PrepareStorage(b,1<<20,denied,&owners).ok());
                assert(controls==before_controls && storage_allocations==before_allocations && b.ValidateValues().ok());
                At<Object*>(private_material_slot.data(),0)=nullptr;
            }
            allocate_calls=0;fail_allocation=trial==1?2:0;
            const auto prepared=a.PrepareStorage(b,trial==0?0:1<<20,p,&owners);
            if(trial<2) {
                assert(!prepared.ok() && b.ValidateValues().ok() && storage_allocations==original_allocations);
                assert(At<int>(parent.controller,12)==4 && At<int>(child.controller,12)==2);
                assert(p.Undo().ok() && p.Undo().ok());
            } else {
                assert(prepared.ok() && At<int>(parent.controller,12)==6 && At<int>(child.controller,12)==2);
                const auto prepared_controls=controls;const auto prepared_allocations=storage_allocations;
                At<Object*>(private_material_slot.data(),0)=&objects[4];
                assert(!p.Publish().ok() && common==2 && controls==prepared_controls
                    && storage_allocations==prepared_allocations && b.ValidateValues().ok());
                At<Object*>(private_material_slot.data(),0)=nullptr;
                At<unsigned char>(&objects[6],0x11c)=0x40;
                using Scheduler=SchedulerTest::Sc6ReplaySchedulerState;
                Scheduler previous;previous.base_=base;previous.epoch_=100;
                Scheduler::Tick tick;tick.address=reinterpret_cast<std::uintptr_t>(&objects[6])+0x110;
                tick.owner.object=reinterpret_cast<std::uintptr_t>(&objects[6]);
                tick.prerequisites.resize(1);tick.prerequisites[0][0]=std::byte{0x37};
                SchedulerTest::Store(tick.binding_layout,0xc,std::uint8_t{0x40});
                SchedulerTest::Store(tick.binding_layout,0x20,reinterpret_cast<std::uintptr_t>(tick.prerequisites.data()));
                std::memcpy(reinterpret_cast<void*>(tick.address),tick.binding_layout.data(),tick.binding_layout.size());
                previous.ticks_.push_back(std::move(tick));
                Scheduler::PreparedRestore sp;sp.previous_image_=&previous;sp.base_=base;sp.epoch_=sp.original_epoch_=100;
                sp.excluded_count_=1;sp.excluded_ticks_[0]=previous.ticks_[0].address;sp.excluded_owners_[0]=previous.ticks_[0].owner.object;
                sp.private_owners_={&p,[](const void* p,void* c,bool(*v)(void*,std::uintptr_t)) {
                    return static_cast<const Trace::Prepared*>(p)->VisitPrivateMeshOwners(c,v);
                }};
                assert(previous.ValidateExcludedTick(sp,true).ok());
                ++sp.excluded_owners_[0];assert(!previous.ValidateExcludedTick(sp,true).ok());--sp.excluded_owners_[0];
                ++sp.excluded_count_;assert(!sp.ValidatePrivateOwners().ok());--sp.excluded_count_;
                assert(!p.Publish().ok() && common==2 && b.ValidateValues().ok());
                At<unsigned char>(&objects[6],0x11c)=0; // Controlled scheduler dependency completes exclusion.
                assert(p.Publish().ok() && common==1 && child_value==55);
                assert(previous.ValidateExcludedTick(sp,false).ok());
                child_value=56;assert(!previous.ValidateExcludedTick(sp,false).ok());child_value=55;
                sp.executing_=true;sp.epoch_=200;
                assert(previous.ValidateExcludedTick(sp,false).ok()); // Frozen B uses original epoch.
                ++previous.epoch_;assert(!previous.ValidateExcludedTick(sp,false).ok());--previous.epoch_;
                assert(!p.Commit().ok()); // Private retirement requires settled execution.
                Trace c=a;
                if(trial>=3) {
                    assert(p.BeginExecution(1<<20).ok());common=3;
                    At<unsigned>(c.values_[0].bytes.data(),0)=3;
                    for(std::size_t i=0;i<c.dynamic_count_;++i) {
                        auto& d=c.dynamic_[i];std::memcpy(d.header.data(),d.address,d.header_size());
                        const auto a=At<Array>(d.address,0);
                        if(a.capacity)d.bytes.assign(a.data,a.data+std::size_t(a.capacity)*16);
                    }
                    assert(c.ValidateValues().ok() && p.SettleExecution(c).ok());
                    if(trial==4) {
                        assert(p.Commit().ok() && common==3 && !objects[5].live && !objects[6].live);
                        assert(At<int>(parent.controller,12)==3 && At<int>(child.controller,8)==0);
                        assert(c.ValidateValues().ok());
                    }
                }
                if(trial!=4) {
                assert(p.Undo().ok() && p.Undo().ok() && common==2 && child_value==55);
                assert(b.ValidateValues().ok() && storage_allocations==original_allocations);
                assert(previous.ValidateExcludedTick(sp,false).ok()); // Trace undo precedes scheduler undo.
                At<unsigned char>(&objects[6],0x11c)=0x40;
                assert(previous.ValidateExcludedTick(sp,true).ok());
                assert(At<int>(parent.controller,12)==4 && At<int>(child.controller,12)==2);
                unsigned seen{};const auto visit=[](void* p,std::uintptr_t){++*static_cast<unsigned*>(p);return true;};
                assert(p.VisitPrivateMeshOwners(&seen,visit).ok() && seen==1);
                At<int>(&objects[0],0x430)=0;assert(!p.VisitPrivateMeshOwners(&seen,visit).ok());At<int>(&objects[0],0x430)=1;
                }
            }
        }
        const auto remaining=storage_allocations;for(auto* p:remaining)Free(p);
    }
    VirtualFree(memory,0,MEM_RELEASE);
    std::puts("Production trace private storage: budget/partial acquisition, scheduler-before-publication, complete held B undo, repeated cleanup and guarded execution pass.");
}
