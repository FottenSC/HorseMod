// Actual production disposal admission and commit methods. Native destruction,
// allocator calls, GC registration and PhysX completion are controlled edges;
// the shipped-native fixture and live replay must prove those implementations.
#include <array>
#include <vector>
#include <memory>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#define NOMINMAX
#include <Windows.h>
#include "deterministic/ReplayPhysicsPublicationLists.hpp"
#define STR(x) x
#define REQUIRE(x) do {if(!(x)){std::printf("failure line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace RC {enum class LogLevel{Default};struct Output {template<LogLevel,class... T>static void send(const char*,T...) {}};}
enum class FailureCode {None,IllegalTransition,UnsupportedContent,PresentationFailed,ContextUnavailable};
struct Status {FailureCode code{};bool ok()const{return code==FailureCode::None;}static Status success(){return {};}static Status failure(FailureCode c){return {c};}};
static unsigned lease_releases{},native_destroys{},native_frees{};
static bool release_fails{},destroy_fails{},free_fails{},lock_fails{};
static std::uintptr_t script{},world_result{};
static std::uintptr_t GetWorld(const void*){return world_result;}
static std::uintptr_t Lookup(const void*,std::uint64_t){return script;}
struct Lease {bool fail{};Status Release(){++lease_releases;return release_fails||fail?Status::failure(FailureCode::ContextUnavailable):Status::success();}};
struct Physics {unsigned releases{};bool fail{};bool ReleaseCommittedStorage(){++releases;return !fail;}const char* failed_check()const{return "controlled_physics";}};
class Sc6ReplayGroundDebrisState {
public:
    struct Mesh {std::uintptr_t component{},root{};};
    struct State {
        struct Array {std::uintptr_t data{};int count{},capacity{};};
        struct Root {std::uintptr_t object{},actor{},controller{};Array ring;};
        enum class Physics {Detached};Physics physics_phase{Physics::Detached};
        enum class Retirement {Retained,Ready,Retiring,Retired,Poisoned};Retirement retirement{Retirement::Retained};
        unsigned retired_roots{};std::size_t count{1};std::uintptr_t base{},manager{},world{};DWORD thread{GetCurrentThreadId()};
        std::array<Root,4> roots{};std::vector<Mesh> meshes;
        struct Material {std::uintptr_t slot{},object{};};std::vector<Material> materials;
        struct BodyProof {std::uintptr_t actor{},body{};};std::array<BodyProof,32> body_proofs{};unsigned body_proof_count{1};
        std::array<std::unique_ptr<::Physics>,2> physics;std::array<std::uintptr_t,2> scenes{};
        Horse::Deterministic::ReplayPhysicsPublicationLists publication;
        std::unique_ptr<Lease> lease{std::make_unique<Lease>()},configuration_lease;const char* physics_check{};bool quiescent{true};
        struct SceneLock {std::uintptr_t scene{},module{};bool Enter(){return !lock_fails;}};
        template<class T>static bool read(std::uintptr_t p,T& v)noexcept {
            __try {if(!p)return false;std::memcpy(&v,reinterpret_cast<void*>(p),sizeof(v));return true;}
            __except(EXCEPTION_EXECUTE_HANDLER){return false;}
        }
        template<class T>static void put(std::uintptr_t p,const T& v){std::memcpy(reinterpret_cast<void*>(p),&v,sizeof(v));}
        bool engine_physics_quiescent(){return quiescent;}
        bool free_pointer(std::uintptr_t){++native_frees;return !free_fails;}
        bool destroy_root(std::uintptr_t root){
            ++native_destroys;if(destroy_fails)return false;
            put(root+0x188,0x10000000u);put(root+0x828,0);put(root+0x8c0,0);
            for(const auto& mesh:meshes)if(mesh.root==root){put(mesh.component+0x188,0x10000000u);
                put(mesh.component+0x790,std::uintptr_t{});put(mesh.component+0x520,std::uintptr_t{});put(mesh.component+0x528,std::uintptr_t{});}
            return true;
        }
#include "ground_disposal_domain.inl"
    };
    std::unique_ptr<State> state_{std::make_unique<State>()};const char* failed_check_{};bool frozen{true};
    bool empty()const{return !state_||!state_->count;}
    Status ValidateFrozen()const{return frozen?Status::success():Status::failure(FailureCode::UnsupportedContent);}
    Status PrepareCommit()noexcept;Status CommitNativeOwners()noexcept;Status ReleaseCommittedOwners()noexcept;
};
#include "ground_commit_methods.inl"
struct Case {
    void* image{VirtualAlloc(nullptr,0x4400000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE)};
    Sc6ReplayGroundDebrisState owner;
    std::uintptr_t root{},mesh{},actor{},ring{},mid{},materials{},body{},outputs{};
    Case() {
        REQUIRE(image);lease_releases=native_destroys=native_frees=0;
        release_fails=destroy_fails=free_fails=lock_fails=false;
        auto& s=*owner.state_;s.base=reinterpret_cast<std::uintptr_t>(image);
        const auto base=s.base;s.world=world_result=base+0x16000;root=base+0x1000;mesh=base+0x3000;actor=base+0x5000;s.manager=base+0x7000;
        ring=base+0x9000;mid=base+0xb000;materials=base+0xc000;body=base+0xe000;outputs=base+0x11000;
        s.roots[0]={root,actor,base+0xa000,{ring,1,1}};s.meshes.push_back({mesh,root});
        s.body_proofs[0]={body,mesh+0x430};s.physics[0]=std::make_unique<Physics>();
        const auto put=[](auto p,auto v){StatePut(p,v);};
        put(actor,base+0x3499188);put(base+0x3499188+0x138,base+0x1c204e0);
        put(root,std::uintptr_t(base+0x33566f8));put(mesh,std::uintptr_t(base+0x36cefb0));
        for(auto component:{root,mesh}) {put(component+0x188,1u);put(component+0x190,actor);}
        put(mesh+0x520,body);put(root+0x828,1);put(root+0x8c0,3);
        put(ring,mesh);put(ring+8,StateArray{materials,1,1});put(materials,mid);
        s.materials.push_back({materials,mid});put(mid,base+0x391ee70);put(mid+0x20,root);
        put(actor+0x2c0,StateArray{base+0xd000,2,2});put(actor+0x2d0,3u);put(actor+0x2e8,2);
        put(base+0xd000,root);put(base+0xd010,mesh);
        for(bool is_mesh:{false,true}) {
            const auto table=base+(is_mesh?0x36cefb0:0x33566f8);
            for(auto [offset,value]:std::array<std::pair<unsigned,unsigned>,8>{{
                {0x360,is_mesh?0x1d99730u:0x898e30u},{0x370,0x1da5910},{0x2d8,0x1da9000},{0x2f0,0x1d42de0},
                {0x2f8,0x1d666f0},{0x288,is_mesh?0x1dd1180u:0x1da66a0u},{0x2b0,0x1d99c50},{0x2c0,is_mesh?0x1dd0fb0u:0x1da5e40u}}})put(table+offset,base+value);
        }
        for(const auto table:{base+0x36cefb0,base+0x33566f8}) {
            put(table+0x630,base+0x20431d0);put(table+0x670,base+0x2057160);put(table+0x678,base+0x20570e0);
        }
        put(base+0x3510a68+0x48,base+0x8955b0);
        constexpr unsigned offsets[]{0x898e30,0x1d99730,0x1d41970,0x1d43340,0x1dd0fb0,0x20289d0};
        constexpr unsigned char bytes[][16]{
            {0x48,0x89,0x5c,0x24,8,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0x0f,0xb6,0xda},
            {0x48,0x8b,0xc4,0x88,0x50,0x10,0x55,0x56,0x41,0x55,0x48,0x8b,0xec,0x48,0x81,0xec},
            {0x40,0x53,0x48,0x83,0xec,0x20,0x8b,0x81,0x88,1,0,0,0x48,0x8b,0xd9,0x0f},
            {0x40,0x53,0x48,0x83,0xec,0x20,0xf6,0x81,0x88,1,0,0,4,0x48,0x8b,0xd9},
            {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0x82,0x4e,0xfd,0xff,0x48,0x8b},
            {0x40,0x53,0x55,0x56,0x48,0x83,0xec,0x20,0x48,0x8b,0xe9,0x48,0x89,0x7c,0x24,0x40}};
        for(unsigned i=0;i<6;++i)std::memcpy(reinterpret_cast<void*>(base+offsets[i]),bytes[i],16);
        script=base+0x14000;std::array<unsigned char,12> jump{0x48,0xb8};
        const auto lookup=reinterpret_cast<std::uintptr_t>(&Lookup);std::memcpy(jump.data()+2,&lookup,8);jump[10]=0xff;jump[11]=0xe0;
        std::memcpy(reinterpret_cast<void*>(base+0xf6e0e0),jump.data(),jump.size());
        const auto get_world=reinterpret_cast<std::uintptr_t>(&GetWorld);std::memcpy(jump.data()+2,&get_world,8);
        std::memcpy(reinterpret_cast<void*>(base+0x1c204e0),jump.data(),jump.size());
        FlushInstructionCache(GetCurrentProcess(),image,0x4400000);
        using List=Horse::Deterministic::ReplayPhysicsPublicationLists;
        put(base+0x12000,mesh+0x430);put(outputs+0x310,List::Array{base+0x12000,1,1});
        const std::array bodies{mesh+0x430};
        const auto read=[](auto p,auto& v){return Sc6ReplayGroundDebrisState::State::read(p,v);};
        REQUIRE(s.publication.Prepare(outputs,bodies,read));
        REQUIRE(s.publication.Detach(read,[](auto p,const auto& v){StatePut(p,v);return true;}));
        put(outputs+0x310,List::Array{base+0x13000,0,1});
    }
    using StateArray=Sc6ReplayGroundDebrisState::State::Array;
    template<class T>static void StatePut(std::uintptr_t p,T v){std::memcpy(reinterpret_cast<void*>(p),&v,sizeof(v));}
    ~Case(){owner.state_.reset();VirtualFree(image,0,MEM_RELEASE);}
};
struct Sc6ReplayParticleCopy {enum class Phase {RetiredCommit,Released};};
enum class ParticleCopyAction {CompleteRetirement,Finish};
enum class RestoreOperationPhase {Committing,Failed};
enum class InteriorPhase {Holding,Failed};
enum class SurfaceCommand {FinishDisplayCommit};
struct HostRetirementGate {
    struct Transaction {bool retirement_requested{};Sc6ReplayGroundDebrisState& ground;
        void* execution{};bool display_finished{},display_finish_requested{};} transaction;
    struct Copy {Sc6ReplayParticleCopy::Phase phase{Sc6ReplayParticleCopy::Phase::RetiredCommit};} copy;
    struct Operation {const char* participant{};FailureCode failure{};RestoreOperationPhase phase{RestoreOperationPhase::Committing};bool pending{};} operation;
    InteriorPhase interior_phase_{InteriorPhase::Holding};unsigned completed{};
    std::vector<ParticleCopyAction> actions;
    unsigned fresh_releases{};bool fresh_pending{};
    Status ReleaseFreshParticleOwnership(bool commit){REQUIRE(commit);++fresh_releases;return fresh_pending?Status::failure(FailureCode::ContextUnavailable):Status::success();}
    bool queue(ParticleCopyAction action){actions.push_back(action);return true;}
    unsigned display_queues{};bool display_queue_ok=true;
    bool QueueSurface(SurfaceCommand){++display_queues;return display_queue_ok;}
    void Advance() {
#include "ground_host_retirement_gate.inl"
        ++completed;
    }
};
int main() {
    using Retirement=Sc6ReplayGroundDebrisState::State::Retirement;
    {
        Case c;REQUIRE(c.owner.PrepareCommit().ok());REQUIRE(!native_destroys&&!native_frees&&!lease_releases);
        REQUIRE(!c.owner.ReleaseCommittedOwners().ok());REQUIRE(c.owner.CommitNativeOwners().ok());
        REQUIRE(native_destroys==1&&native_frees==1&&!lease_releases);
        REQUIRE(c.owner.CommitNativeOwners().ok()&&native_destroys==1&&native_frees==1);
        release_fails=true;REQUIRE(!c.owner.ReleaseCommittedOwners().ok());REQUIRE(c.owner.state_);
        release_fails=false;REQUIRE(c.owner.ReleaseCommittedOwners().ok());REQUIRE(!c.owner.state_&&lease_releases==2);
    }
    {
        Case c;Case::StatePut(c.root+0x188,5u);
        // Native141FFB640 tests actual body ownership, not the created flag.
        // An empty root must pass without erasing the flag or touching B.
        REQUIRE(c.owner.PrepareCommit().ok());
        REQUIRE(*reinterpret_cast<unsigned*>(c.root+0x188)==5u&&!native_destroys&&!native_frees);
    }
    for(const unsigned offset:{0xf0u,0xf8u,0x210u,0xb8u,0xb0u,0x100u}) {
        Case c;Case::StatePut(c.root+0x188,5u);Case::StatePut(c.root+0x430+offset,c.body);
        REQUIRE(!c.owner.PrepareCommit().ok()&&!native_destroys&&!native_frees);
    }
    for(unsigned fault=0;fault<5;++fault) {
        Case c;Case::StatePut(c.root+0x188,5u);const auto base=c.owner.state_->base;
        if(fault==0)Case::StatePut(c.root+0xa0+0x50,1);
        if(fault==1)Case::StatePut(base+0x40939e0+0x64,1);
        if(fault==2)Case::StatePut(base+0x33566f8+0x630,base+0x20431d1);
        if(fault==3)Case::StatePut(c.root+0x1e0,1);
        if(fault==4)Case::StatePut(c.root+0x3f8,static_cast<unsigned char>(2));
        REQUIRE(!c.owner.PrepareCommit().ok()&&!native_destroys&&!native_frees);
    }
    {
        Case c;Case::StatePut(c.mesh+0x188,0x0a2c0607u);
        // Native unregister reaches no navigation owner when the verified
        // owning world has a null NavigationSystem. Preserve the flags.
        REQUIRE(c.owner.PrepareCommit().ok());
        REQUIRE(*reinterpret_cast<unsigned*>(c.mesh+0x188)==0x0a2c0607u&&!native_destroys&&!native_frees);
    }
    for(unsigned fault=0;fault<4;++fault) {
        Case c;Case::StatePut(c.mesh+0x188,0x0a2c0607u);auto& s=*c.owner.state_;
        if(fault==0) {Case::StatePut(s.world+0xe8,s.base+0x18000);Case::StatePut(s.base+0x18000+0x238,c.root);}
        if(fault==1)world_result=s.world+8;
        if(fault==2)Case::StatePut(s.base+0x3499188+0x138,s.base+0x1c204e1);
        if(fault==3)s.world=0;
        REQUIRE(!c.owner.PrepareCommit().ok()&&!native_destroys&&!native_frees);
    }
    for(bool suppressed:{false,true}) {
        Case c;auto& s=*c.owner.state_;Case::StatePut(c.mesh+0x188,0x0a2c0607u);
        Case::StatePut(s.world+0xe8,s.base+0x18000);
        if(suppressed) {Case::StatePut(s.base+0x18000+0x238,c.root);Case::StatePut(s.base+0x18000+0x3fa,static_cast<unsigned char>(1));}
        REQUIRE(c.owner.PrepareCommit().ok()&&!native_destroys&&!native_frees);
    }
    for(unsigned fault=0;fault<11;++fault) {
        Case c;auto& s=*c.owner.state_;const auto put=Case::StatePut<unsigned>;
        switch(fault) {
        case 0:put(c.mesh+0x188,0x10000001);break;
        case 1:Case::StatePut(c.mesh+0x430+0xb8,c.root);break;
        case 2:Case::StatePut(c.mid+0x20,c.mesh);break;
        case 3:Case::StatePut(c.ring+0x18,Case::StateArray{c.materials,1,1});break;
        case 4:put(c.actor+0x2d0,1);break;
        case 5:put(c.mesh+0x188,5);put(s.base+0x40939e0+0x50,1);break;
        case 6:Case::StatePut(c.root+0x8d0,c.mesh);break;
        case 7:put(c.root+0x188,0x8000001);put(script+0x50,1);break;
        case 8:Case::StatePut(s.manager+0x3f8,Case::StateArray{c.ring,1,1});Case::StatePut(c.ring,c.root);break;
        case 9:put(s.base+0x898e30,0);break;
        case 10:Case::StatePut(c.body+0x80,c.root);break;
        }
        REQUIRE(!c.owner.PrepareCommit().ok());REQUIRE(!native_destroys&&!native_frees&&!lease_releases);
    }
    for(unsigned fault=0;fault<3;++fault) {
        Case c;REQUIRE(c.owner.PrepareCommit().ok());
        if(fault==0)lock_fails=true;if(fault==1)free_fails=true;if(fault==2)destroy_fails=true;
        REQUIRE(!c.owner.CommitNativeOwners().ok());REQUIRE(c.owner.state_->retirement==Retirement::Poisoned);
        const auto destroys=native_destroys,frees=native_frees;
        REQUIRE(!c.owner.CommitNativeOwners().ok()&&!c.owner.ReleaseCommittedOwners().ok());
        REQUIRE(native_destroys==destroys&&native_frees==frees&&!lease_releases);
    }
    {
        Case c;REQUIRE(c.owner.PrepareCommit().ok()&&c.owner.CommitNativeOwners().ok());
        HostRetirementGate host{{false,c.owner}};
        host.Advance();REQUIRE(!lease_releases&&!host.completed&&host.actions.back()==ParticleCopyAction::CompleteRetirement);
        host.Advance();REQUIRE(!lease_releases&&!host.completed&&host.actions.back()==ParticleCopyAction::Finish);
        // The actual host prefix cannot release native B until render-thread
        // Finish reports Released after the ordered GPU completion.
        REQUIRE(!host.fresh_releases);
        host.copy.phase=Sc6ReplayParticleCopy::Phase::Released;host.fresh_pending=true;
        host.Advance();REQUIRE(host.fresh_releases==1 && !lease_releases && !host.completed);
        host.fresh_pending=false;release_fails=true;
        host.Advance();REQUIRE(lease_releases==1&&!host.completed&&c.owner.state_);
        release_fails=false;host.Advance();REQUIRE(lease_releases==2&&host.completed==1&&!c.owner.state_);
        host.Advance();REQUIRE(lease_releases==2&&native_destroys==1&&native_frees==1);
    }
    for(bool accepted:{false,true}) {
        Case c;REQUIRE(c.owner.PrepareCommit().ok() && c.owner.CommitNativeOwners().ok());
        HostRetirementGate host{{true,c.owner}};host.transaction.execution=&host;
        host.copy.phase=Sc6ReplayParticleCopy::Phase::Released;host.display_queue_ok=accepted;
        host.Advance();REQUIRE(host.display_queues==1 && !host.fresh_releases && !lease_releases && !host.completed);
        if(!accepted) {REQUIRE(host.operation.phase==RestoreOperationPhase::Failed);continue;}
        host.Advance();REQUIRE(host.display_queues==1 && !host.fresh_releases && !lease_releases);
        host.transaction.display_finished=true;host.Advance();
        REQUIRE(host.fresh_releases==1 && lease_releases==1 && host.completed==1);
    }
    {
        Case c;c.owner.state_->configuration_lease=std::make_unique<Lease>();
        REQUIRE(c.owner.PrepareCommit().ok()&&c.owner.CommitNativeOwners().ok());
        c.owner.state_->configuration_lease->fail=true;
        REQUIRE(!c.owner.ReleaseCommittedOwners().ok()&&c.owner.state_);
        REQUIRE(lease_releases==2&&native_destroys==1);
        c.owner.state_->configuration_lease->fail=false;
        REQUIRE(c.owner.ReleaseCommittedOwners().ok()&&!c.owner.state_&&native_destroys==1);
    }
    std::puts("Production ground commit admission and retirement contracts passed");
}
