// The full production RetainOwners body, with only native reachability/lease
// acquisition controlled. Assert the objects actually offered to the lease;
// successful binding validation is not a substitute for strong ownership.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <vector>
#include <algorithm>
enum class FailureCode {CapacityExceeded, GenerationMismatch, ContextUnavailable, WrongThread, IllegalTransition};
struct Status {
    bool good{};bool ok()const{return good;}
    static Status success(){return {true};}
    static Status failure(FailureCode){return {false};}
};
struct Sc6ReplayObjectLease {
    static inline bool reject{};
    static inline unsigned calls{},reject_call{},release_calls{};
    static inline void* dead_object{};
    bool live{true},released{},reject_release{};
    std::vector<void*> offered;
    Status Acquire(std::uintptr_t,std::span<void* const> objects,std::size_t) {
        ++calls;offered.assign(objects.begin(),objects.end());return {!reject && calls!=reject_call};
    }
    std::size_t owned_bytes()const{return 64+offered.size()*24;}
    Status Validate()const{return {live && !released && (!dead_object || std::find(offered.begin(),offered.end(),dead_object)==offered.end())};}
    Status ValidateObject(const void* object)const{return {Validate().ok() && std::find(offered.begin(),offered.end(),object)!=offered.end()};}
    Status Release(){++release_calls;if(reject_release)return {false};released=true;return {true};}
};
unsigned GetCurrentThreadId(){return 7;}
bool Live(std::uintptr_t,const std::array<int,2>&,std::uintptr_t){return true;}
bool CopyBytes(void* a,const void* b,std::size_t n){std::memcpy(a,b,n);return true;}
struct Image {
    std::uintptr_t event{},second{},type{},mesh{},field{};
    auto event_module()const{return event;}auto second_instance_module()const{return second;}
    auto emitter_type_data()const{return type;}auto mesh_asset()const{return mesh;}
    auto vector_field_asset()const{return field;}
};
struct Sc6ReplayVfxState {
    struct Component {
        std::uintptr_t address{},particle_template{},attach_parent{};std::array<int,2> weak{};bool lux{true};
        struct {std::size_t count{};std::array<std::array<std::byte,16>,16> entries{};} completions;
        struct Root {struct Entry {std::uintptr_t object{},parent{};};int count{};std::array<Entry,32> entries{};};
        std::array<Root,2> material_roots;
        std::vector<std::uintptr_t> emitters;
        struct {std::uintptr_t world{},owner{},manager{};} events;
    };
    using ComponentBinding=Component;
    struct ReconstructionBinding {struct Identity {std::uintptr_t source{},target{};std::array<int,2> source_weak{},target_weak{};} identity;const Sc6ReplayObjectLease* lease{};};
    Status ValidateReconstructionBindings(std::span<const ReconstructionBinding>) const noexcept;
    struct Emitter {Image image;};
    std::vector<Component> components_;
    std::vector<Emitter> cpu_emitters_,gpu_owners_;
    std::uintptr_t base_{},manager_{1};
    std::unique_ptr<Sc6ReplayObjectLease> object_lease_, reconstruction_lease_;
    std::size_t owned_bytes()const{return (object_lease_?object_lease_->owned_bytes():0)+(reconstruction_lease_?reconstruction_lease_->owned_bytes():0);}
    bool valid_{true};unsigned thread_{7};
    Status ReleaseOwners() noexcept;
    Status ValidateReconstructionDependencies() const noexcept;
    Status RetainOwners(std::size_t,std::span<void* const>);
};
#include "vfx_retain_owners.inl"
struct TraceOwnership {
    struct Object {int index{},serial{1};bool alive{true};};
    struct Id {Object* object{};int index{},serial{};};
    struct State {Id mesh;};
    bool captured_{true};unsigned thread_{7};std::vector<State> states_;
    Sc6ReplayObjectLease lease_;
    static bool Live(const Id& id){return id.object && id.object->alive && id.object->index==id.index && id.object->serial==id.serial;}
#include "trace_capture_mesh_ownership.inl"
};
struct CaptureHost {
    struct Output {TraceOwnership* traces;};
    struct Copy {std::vector<void*> owners;auto lighting_owners()const{return std::span<void* const>(owners);}};
    Copy* capture_copy{};
    Status Collect(Output& output,std::array<void*,4096>& physics_owners,std::size_t& physics_owner_count){
#include "host_lighting_companions.inl"
        return Status::success();
    }
};
bool TestTraceLightingOwnership() {
    TraceOwnership::Object mesh{17,23,true},other{18,24,true};
    TraceOwnership trace;trace.states_.push_back({{&mesh,17,23}});
    void* retained=&mesh;if(!trace.lease_.Acquire(0,{&retained,1},10000).ok())return false;
    CaptureHost::Copy copy{{&mesh,&other}};CaptureHost host{&copy};CaptureHost::Output output{&trace};
    std::array<void*,4096> owners{};std::size_t count{};
    if(!host.Collect(output,owners,count).ok())return false;
    Sc6ReplayVfxState vfx;if(!vfx.RetainOwners(1024*1024,{owners.data(),count}).ok())return false;
    Sc6ReplayObjectLease::dead_object=&mesh;mesh.alive=false;
    if(vfx.ValidateReconstructionDependencies().ok()==false){std::puts("trace-owned lighting mesh incorrectly remains mandatory VFX reconstruction dependency");return false;}
    if(trace.RetainsCapturedMesh(&mesh))return false;
    Sc6ReplayObjectLease::dead_object=&other;
    if(vfx.ValidateReconstructionDependencies().ok())return false;
    Sc6ReplayObjectLease::dead_object=nullptr;mesh.alive=true;
    // A separate physics role must still retain the original identity.
    count=1;owners[0]=&mesh;
    if(!host.Collect(output,owners,count).ok())return false;
    Sc6ReplayVfxState physics;if(!physics.RetainOwners(1024*1024,{owners.data(),count}).ok())return false;
    Sc6ReplayObjectLease::dead_object=&mesh;
    if(physics.ValidateReconstructionDependencies().ok())return false;
    Sc6ReplayObjectLease::dead_object=nullptr;
    for(unsigned fault=0;fault<7;++fault) {
        trace.captured_=true;trace.thread_=7;trace.lease_.live=true;mesh.alive=true;mesh.serial=23;trace.states_[0].mesh.object=&mesh;
        switch(fault){case 0:trace.captured_=false;break;case 1:trace.thread_=8;break;case 2:trace.lease_.live=false;break;case 3:mesh.alive=false;break;case 4:mesh.serial=24;break;case 5:trace.states_[0].mesh.object=&other;break;case 6:trace.lease_.offered.clear();break;}
        if(trace.RetainsCapturedMesh(&mesh) || trace.RetainsCapturedMesh(nullptr))return false;
        count=0;if(!host.Collect(output,owners,count).ok() || std::find(owners.begin(),owners.begin()+count,&mesh)==owners.begin()+count)return false;
    }
    output.traces=nullptr;count=0;
    if(!host.Collect(output,owners,count).ok() || count!=2)return false;
    return true;
}
int main() {
    if(!TestTraceLightingOwnership())return 26;
    Sc6ReplayVfxState vfx;
    vfx.cpu_emitters_.push_back({{2,3,4,5,0}});
    vfx.gpu_owners_.push_back({{6,7,8,0,9}});
    auto result=vfx.RetainOwners(1024*1024,{});
    if(!result.ok() || !vfx.object_lease_)return 1;
    if(!vfx.reconstruction_lease_) { std::puts("missing independent reconstruction dependency lease");return 6; }
    for(std::uintptr_t owner=1;owner<=9;++owner) {
        if(std::count(vfx.reconstruction_lease_->offered.begin(),vfx.reconstruction_lease_->offered.end(),reinterpret_cast<void*>(owner))!=1)return 7;
        if(std::count(vfx.object_lease_->offered.begin(),vfx.object_lease_->offered.end(),reinterpret_cast<void*>(owner))!=1) {
            std::printf("GPU/CPU dependency %zu was not offered exactly once to the native lease\n",owner);return 2;
        }
    }
    // Shared module/asset ownership is charged and acquired once, not once per emitter.
    vfx.gpu_owners_.push_back(vfx.gpu_owners_[0]);
    if(!vfx.RetainOwners(1024*1024,{}).ok() || vfx.object_lease_->offered.size()!=9)return 3;
    Sc6ReplayObjectLease::calls=0;
    if(vfx.RetainOwners(1,{}).ok() || Sc6ReplayObjectLease::calls)return 4;
    Sc6ReplayObjectLease::reject=true;
    if(vfx.RetainOwners(1024*1024,{}).ok() || Sc6ReplayObjectLease::calls!=1)return 5;
    Sc6ReplayObjectLease::reject=false;
    Sc6ReplayObjectLease::calls=0;Sc6ReplayObjectLease::reject_call=2;
    Sc6ReplayVfxState partial;
    if(partial.RetainOwners(1024*1024,{}).ok() || !partial.object_lease_)return 8;
    if(!partial.ReleaseOwners().ok() || partial.object_lease_ || partial.reconstruction_lease_)return 9;
    Sc6ReplayObjectLease::reject_call=0;
    Sc6ReplayVfxState separated;
    std::array<std::byte,0x810> component{};std::uintptr_t asset=10;
    std::memcpy(component.data()+0x808,&asset,8);
    Sc6ReplayVfxState::Component row{};row.address=reinterpret_cast<std::uintptr_t>(component.data());row.particle_template=asset;
    separated.components_.push_back(row);
    void* companion=component.data();
    if(!separated.RetainOwners(1024*1024,{&companion,1}).ok())return 10;
    if(std::count(separated.object_lease_->offered.begin(),separated.object_lease_->offered.end(),companion)!=1
        || std::count(separated.reconstruction_lease_->offered.begin(),separated.reconstruction_lease_->offered.end(),companion)
        || separated.reconstruction_lease_->offered.size()!=2)return 11;
    std::array<std::byte,0x810> fresh{};
    const std::uintptr_t vt=0x335db28;std::memcpy(fresh.data(),&vt,8);std::memcpy(fresh.data()+0x808,&asset,8);
    std::memcpy(component.data(),&vt,8);
    Sc6ReplayObjectLease private_lease;void* fresh_object=fresh.data();private_lease.Acquire(0,{&fresh_object,1},10000);
    Sc6ReplayVfxState::ReconstructionBinding mapping{{row.address,reinterpret_cast<std::uintptr_t>(fresh.data()),{0,0},{1,2}},&private_lease};
    const auto check=[&]{return separated.ValidateReconstructionBindings({&mapping,1}).ok();};
    if(!check() || separated.ValidateReconstructionBindings({}).ok())return 18;
    mapping.identity.source_weak={9,9};if(check())return 19;mapping.identity.source_weak={0,0};
    mapping.lease=separated.reconstruction_lease_.get();if(check())return 20;mapping.lease=&private_lease;
    private_lease.live=false;if(check())return 21;private_lease.live=true;
    std::uintptr_t bad_asset=11;std::memcpy(fresh.data()+0x808,&bad_asset,8);if(check())return 22;std::memcpy(fresh.data()+0x808,&asset,8);
    mapping.identity.target=row.address;mapping.identity.target_weak={0,0};mapping.lease=separated.object_lease_.get();
    if(!check())return 23;
    mapping.identity.target=reinterpret_cast<std::uintptr_t>(fresh.data());mapping.identity.target_weak={1,2};mapping.lease=&private_lease;
    separated.object_lease_->live=false;
    component.fill(std::byte{0xdd});if(!check())return 24;
    auto duplicate=mapping;std::array duplicated{mapping,duplicate};
    if(separated.ValidateReconstructionBindings(duplicated).ok())return 25;

    if(!separated.ValidateReconstructionDependencies().ok())return 12;
    separated.reconstruction_lease_->live=false;
    if(separated.ValidateReconstructionDependencies().ok())return 13;
    separated.reconstruction_lease_->live=true;separated.thread_=8;
    if(separated.ValidateReconstructionDependencies().ok())return 14;
    separated.thread_=7;separated.reconstruction_lease_->reject_release=true;
    Sc6ReplayObjectLease::release_calls=0;
    if(separated.ReleaseOwners().ok() || separated.valid_ || separated.object_lease_ || !separated.reconstruction_lease_)return 15;
    separated.reconstruction_lease_->reject_release=false;
    if(!separated.ReleaseOwners().ok() || separated.reconstruction_lease_ || Sc6ReplayObjectLease::release_calls!=3)return 16;
    if(separated.ValidateReconstructionDependencies().ok())return 17;
    Sc6ReplayVfxState event_owners;
    row.address=reinterpret_cast<std::uintptr_t>(fresh.data());
    row.events={12,13,14};event_owners.components_.push_back(row);
    if(!event_owners.RetainOwners(1024*1024,{}).ok())return 27;
    for(std::uintptr_t owner=12;owner<=14;++owner)
        if(std::count(event_owners.object_lease_->offered.begin(),event_owners.object_lease_->offered.end(),reinterpret_cast<void*>(owner))!=1)return 28;
    std::puts("production GPU/CPU dependency collection reaches lease acquisition; capacity and acquisition failures propagate");
}
