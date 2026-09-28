#include "ReplayGroundDebrisConfiguration.hpp"
#include "ReplayGroundBodyConfiguration.hpp"
#include "ReplayPhysicsStepConsumer.hpp"
#include "ReplayPhysicsBodyInventory.hpp"
#include <Windows.h>
#include "../HorseMod/horselib/deterministic/ReplayGroundPose.hpp"
#include "../HorseMod/horselib/deterministic/ReplayGroundInitialBodyState.hpp"
using Horse::Deterministic::ReplayGroundPose;
using Horse::Deterministic::ReplayGroundInitialBodyState;
using Horse::Deterministic::ReplayGroundShapeState;
using Horse::Deterministic::ReplayPhysicsBodyInventory;
#include <array>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <vector>
namespace RC::Unreal {
// Controlled reflection lookup edge. The production function still verifies
// the returned native target, script state, FName and manager dispatch slot.
struct UFunction {
    inline static std::uintptr_t native{};
    inline static int script_count{};
    inline static unsigned function_flags{0x400};
    struct Script {int Num()const{return script_count;}};
    void* GetFuncPtr()const{return reinterpret_cast<void*>(native);}
    unsigned GetFunctionFlags()const{return function_flags;}
    Script GetScript()const{return {};}
};
struct UObject {
    inline static UFunction* event{};
    UFunction* GetFunctionByNameInChain(const wchar_t*)const{return event;}
    int GetInternalIndex()const{int index{};std::memcpy(&index,reinterpret_cast<const std::byte*>(this)+0xc,4);return index;}
};
struct FUObjectItem {
    UObject* object{};bool valid{true};int serial{};
    bool IsValid(bool)const{return valid;}
    UObject* GetUObject()const{return object;}
    int GetSerialNumber()const{return serial;}
};
struct FUObjectArray {
    inline static std::array<FUObjectItem,4> items{};
    static const FUObjectItem* IndexToObject(int index){return index>=0 && index<int(items.size())?&items[index]:nullptr;}
};
}
using Horse::Deterministic::ReplayGroundDebrisConfiguration;
using Horse::Deterministic::ReplayGroundBodyConfiguration;
enum class FailureCode {None,IllegalTransition,GenerationMismatch,UnsupportedContent};
struct Status {FailureCode code{};bool ok()const{return code==FailureCode::None;}static Status success(){return {};}static Status failure(FailureCode code){return {code};}};
struct Lease {Status Validate()const{return Status::success();}};
struct State {
    struct Array {std::uintptr_t data{};int count{},capacity{};friend bool operator==(const Array&,const Array&)=default;};
#include "ground_root_fields.inl"
#include "ground_mesh_fields.inl"
    std::uintptr_t base{},manager{},world{};std::uint64_t deactivation_name{};
    ReplayPhysicsBodyInventory body_inventory{};
    DWORD thread{GetCurrentThreadId()};std::size_t count{};
    std::array<Root,4> roots{};std::vector<Mesh> meshes;
    struct Material {std::uintptr_t slot{},object{};};std::vector<Material> materials;
    Lease lease_storage;Lease* lease{&lease_storage};
    // Controlled native-scene edge; this fixture checks that the production
    // update boundary actually consults it. Native aggregate/filter checks
    // have separate coverage and are not granted by this stub.
    inline static bool collision_admitted=true;
    inline static unsigned collision_calls{};
    void observe_notifications() const {} // Logging only; native ownership covered by SDK fixture.
    bool collision_update(std::span<const std::uintptr_t> children,const char*& check,std::uintptr_t& owner) {
        ++collision_calls;check="update_collision_domain";owner=children.empty()?0:children.front();return collision_admitted;
    }
    template<class T> static bool read(std::uintptr_t p,T& value) {
        __try {if(!p)return false;std::memcpy(&value,reinterpret_cast<void*>(p),sizeof(value));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    // Controlled weak binding edge; field reader/layout predicates are production.
    static bool live(std::uintptr_t,std::uintptr_t object,const std::array<std::int32_t,2>& weak){return object && weak==std::array<std::int32_t,2>{1,2};}
    inline static unsigned weak_bind_calls{};
    static bool bind(std::uintptr_t,std::uintptr_t object,std::array<std::int32_t,2>& weak){++weak_bind_calls;weak={1,2};return object!=0;}
#include "ground_dormant_tick.inl"
#include "ground_root_read.inl"
#include "ground_mesh_read.inl"
#include "ground_same_root.inl"
};
struct Sc6ReplayGroundDebrisState {
    using State=::State;State* state_{};
    Status ValidateFrozen()const noexcept;
#include "ground_update_diagnostic_fields.inl"
    static Status ValidateUpdate(std::uintptr_t,void*,void*,UpdateDiagnostic&)noexcept;
};
#include "ground_validate_frozen.inl"
#include "ground_validate_update.inl"
template<class T> void put(std::uintptr_t p,const T& value){std::memcpy(reinterpret_cast<void*>(p),&value,sizeof(value));}
std::uintptr_t settings_world{},settings_result{};unsigned settings_calls{};
std::uintptr_t __fastcall settings_lookup(const void* world,bool check_streaming,bool checked) {
 ++settings_calls;assert(reinterpret_cast<std::uintptr_t>(world)==settings_world && check_streaming && checked);return settings_result;
}
int main(){
 auto* memory=VirtualAlloc(nullptr,0x4500000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(memory);
 State state;state.base=reinterpret_cast<std::uintptr_t>(memory);state.manager=state.base+0x8000;state.deactivation_name=42;
 const auto b=state.base,root=b+0x1000,actor=b+0x3000,ring=b+0x4000,delegate=b+0x5000,controller=b+0x6000,world=b+0x12000;
 state.world=world;settings_world=world;settings_result=actor;
 const auto physics_scene=b+0x1a000;put(world+0x1c8,physics_scene);
 put(actor+0xc,3);RC::Unreal::FUObjectArray::items[3].object=reinterpret_cast<RC::Unreal::UObject*>(actor);
 put(b+0x3499188+0x138,b+0x1c204e0);put(b+0x3499188+0x5e8,b+0x2d72f0);
 const auto executable=[&](std::uintptr_t entry,const auto& bytes) {
  put(entry,bytes);DWORD old{};assert(VirtualProtect(reinterpret_cast<void*>(entry),bytes.size(),PAGE_EXECUTE_READ,&old));
  assert(FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(entry),bytes.size()));
 };
 std::array<unsigned char,11> world_return{0x48,0xb8};std::memcpy(world_return.data()+2,&world,8);world_return[10]=0xc3;
 executable(b+0x1c204e0,world_return);
 std::array<unsigned char,12> settings_jump{0x48,0xb8};const auto lookup=reinterpret_cast<std::uintptr_t>(&settings_lookup);
 std::memcpy(settings_jump.data()+2,&lookup,8);settings_jump[10]=0xff;settings_jump[11]=0xe0;executable(b+0x21bc760,settings_jump);
 put(root+0xc,1);RC::Unreal::FUObjectArray::items[1].object=reinterpret_cast<RC::Unreal::UObject*>(root);
 put(root,b+0x33566f8);put(b+0x33566f8+0x300,b+0x8a4860);
 put(b+0x33566f8+0x308,b+0x1d5dbe0);put(b+0x33566f8+0x360,b+0x898e30);
 put(b+0x3510a68+0x38,b+0x2d72f0);put(b+0x3510a68+0x60,b+0x30f4c60);
 constexpr std::array<unsigned char,11> dispatch{0x48,0x8b,0x41,0x10,0x48,0x8b,0x49,8,0x48,0xff,0xe0};
 put(b+0x30f4c60,dispatch);
 put(root+0x10,b+0x9000);
 put(root+0x190,actor);put(actor,b+0x3499188);put(b+0x3499188+0x410,b+0x1c0fe90);
 put(root+0x11c,static_cast<unsigned char>(0x42));put(root+0x160,root);
 put(root+0x808,static_cast<unsigned char>(1));put(root+0x830,static_cast<unsigned char>(2));
 put(root+0x820,State::Array{ring,1,1});put(root+0x810,State::Array{delegate,1,1});
 put(delegate,std::array<std::int32_t,2>{1,2});put(delegate+8,state.deactivation_name);
 put(root+0x8c0,3);put(root+0x8b0,controller);
 put(controller,std::array<std::uintptr_t,6>{b+0x3510a68,root+0x820,b+0x8a0230,0,1,0});
 put(root+0xa08,std::array<unsigned,3>{0x3f800000,0x40000000,0x3d888889});
 // Native FTransform48 starts270; ring impulse reads translation at280.
 const std::array<float,12> transform{0,0,0,1,10,11,12,0,2,3,4,0};
 std::memset(reinterpret_cast<void*>(root+0x260),0xa5,16);put(root+0x270,transform);
 const std::array<float,3> root_relative{10,11,12};put(root+0x2c0,root_relative);
 put(root+0x240,9u);
 put(state.manager,b+0x3356f68);put(b+0x3356f68+0x600,b+0x89f5f0);
 RC::Unreal::UObject::event=reinterpret_cast<RC::Unreal::UFunction*>(b+0xf800);
 RC::Unreal::UFunction::native=b+0xcf2760;put(b+0xf800+0x18,state.deactivation_name);
 State::Root captured;assert(state.read_root(root,captured));
 put(b+0x3356f68+0x600,b+0x89f5f1);
 if(state.read_root(root,captured)){std::puts("ground root admitted changed manager deactivation dispatch");return 33;}
 put(b+0x3356f68+0x600,b+0x89f5f0);
 RC::Unreal::UFunction::native=b+0xcf2761;assert(!state.read_root(root,captured));RC::Unreal::UFunction::native=b+0xcf2760;
 RC::Unreal::UFunction::script_count=1;assert(!state.read_root(root,captured));RC::Unreal::UFunction::script_count=0;
 RC::Unreal::UFunction::function_flags=0;assert(!state.read_root(root,captured));RC::Unreal::UFunction::function_flags=0x400;
 put(b+0xf800+0x18,std::uint64_t{43});assert(!state.read_root(root,captured));put(b+0xf800+0x18,state.deactivation_name);
 RC::Unreal::UObject::event=nullptr;assert(!state.read_root(root,captured));RC::Unreal::UObject::event=reinterpret_cast<RC::Unreal::UFunction*>(b+0xf800);
 assert(state.read_root(root,captured));
 // The native ring dispatcher tail-calls controller+10 with context+8.
 // An arbitrary nonnull callback must not acquire the admitted ring contract.
 put(controller+16,b+0x8a0231);
 if(state.read_root(root,captured)) {std::puts("ground root admitted an unknown ring callback");return 32;}
 put(controller+16,b+0x8a0230);assert(state.read_root(root,captured));
 for(const auto [slot,target]:std::array<std::pair<std::uintptr_t,std::uintptr_t>,4>{{
     {b+0x33566f8+0x308,b+0x1d5dbe0},{b+0x33566f8+0x360,b+0x898e30},
     {b+0x3510a68+0x38,b+0x2d72f0},{b+0x3510a68+0x60,b+0x30f4c60}}}) {
  put(slot,target+1);assert(!state.read_root(root,captured));put(slot,target);assert(state.read_root(root,captured));
 }
 put(b+0x30f4c60,static_cast<unsigned char>(0x90));assert(!state.read_root(root,captured));put(b+0x30f4c60,dispatch);
 put(root+0x8d0,b+0xa000);assert(!state.read_root(root,captured));put(root+0x8d0,std::uintptr_t{});
 for(unsigned offset:{0x8d8u,0x8e8u}) {
  put(root+offset,State::Array{b+0xa000,1,1});assert(!state.read_root(root,captured));put(root+offset,State::Array{});
 }
 put(controller+16,b+0x89ff80);assert(state.read_root(root,captured));put(controller+16,b+0x8a0230);
 assert(state.read_root(root,captured));
 assert(!std::memcmp(captured.transform_auxiliary.data()+56,root_relative.data(),12) && captured.transform_flags==9);
 assert(!std::memcmp(captured.transform.data(),transform.data(),sizeof(transform)));
 assert(captured.clock[0]==0x3f800000 && captured.clock[2]==0x3d888889);
 const auto mesh=b+0xb000,type=b+0x36cefb0;
 put(mesh+0xc,2);RC::Unreal::FUObjectArray::items[2].object=reinterpret_cast<RC::Unreal::UObject*>(mesh);
 RC::Unreal::FUObjectArray::items[2].serial=17418;
 put(ring,mesh);put(mesh,type);put(mesh+0x10,b+0xc000);
 put(mesh+0x20,root);put(mesh+0x190,actor);put(mesh+0x65e,static_cast<unsigned char>(2));
 put(type+0x300,b+0x1d652a0);put(type+0x438,b+0x1da43e0);
 put(type+0x430,b+0x1db1200);put(type+0x428,b+0x1da6a20);put(type+0x480,b+0x2043340);
 put(type+0x448,b+0x1dbe830);
 put(mesh+0x920,b+0xd000);put(mesh+0x420,37u);put(mesh+0x240,9u);put(mesh+0x188,0x80006u);
 const std::array<float,12> child_transform{0,0,1,0,-23,41,67,0,3,2,1,0};
 std::memset(reinterpret_cast<void*>(mesh+0x260),0x5a,16);put(mesh+0x270,child_transform);
 const std::array<float,3> child_relative{-23,41,67};put(mesh+0x2c0,child_relative);
 put(mesh+0x318,static_cast<unsigned char>(2));
 put(mesh+0x808,State::Array{b+0xe000,2,4});
 put(b+0xe000,std::uintptr_t{b+0xf000});put(b+0xe008,std::uintptr_t{});
 const auto body=mesh+0x430;
 put(body+0x74,0x7fffffu);put(body+0xd0,b+0xf100);put(body+0x84,42.0f);
 put(body+0x60,State::Array{b+0xf200,1,2});put(b+0xf200,std::uint64_t{73});put(b+0xf208,static_cast<unsigned char>(2));
 for(auto offset:{0xf0u,0xf8u,0x118u,0x120u,0x138u,0x140u,0x208u,0x210u,0x220u})put(body+offset,std::uintptr_t{0xbadbad});
 State::Mesh captured_mesh;assert(state.read_mesh(captured,0,captured_mesh));
 assert(!std::memcmp(captured_mesh.transform_auxiliary.data()+56,child_relative.data(),12));
 assert(captured_mesh.body_configuration.ready() && captured_mesh.mobility==2);
 assert(captured_mesh.body_configuration.physical_material()==b+0xf100);
 std::array<std::byte,0x230> body_view{};
 assert(captured_mesh.body_configuration.BuildNativeView(body_view));
 unsigned config_flags{};std::memcpy(&config_flags,body_view.data()+0x74,4);
 assert(config_flags==0x3bffff);
 for(auto offset:{0xf0u,0xf8u,0x118u,0x120u,0x138u,0x140u,0x208u,0x210u,0x220u}) {
  std::uintptr_t identity{};std::memcpy(&identity,body_view.data()+offset,8);assert(!identity);
 }
 put(b+0xf200,std::uint64_t{99});
 State::Array body_responses{};std::memcpy(&body_responses,body_view.data()+0x60,sizeof(body_responses));
 std::uint64_t channel{};std::memcpy(&channel,reinterpret_cast<void*>(body_responses.data),8);assert(channel==73);

 assert(!std::memcmp(captured_mesh.transform.data(),child_transform.data(),sizeof(child_transform)));
 assert(captured_mesh.material_count==2 && captured_mesh.material_overrides[0]==b+0xf000 && !captured_mesh.material_overrides[1]);
 // The actual pre-publication validator must recheck child consumers even
 // when the retained UObject/serial and root entry remain unchanged.
 state.count=1;assert(state.read_root(root,state.roots[0]));state.meshes.push_back(captured_mesh);
 Sc6ReplayGroundDebrisState frozen{&state};assert(frozen.ValidateFrozen().ok());
 for(unsigned offset:{0x3b0u,0x1e0u,0x6a8u,0x3d8u,0x3c4u}) {
  put(mesh+offset,1);
  if(frozen.ValidateFrozen().ok()) {std::printf("frozen ground admitted changed child consumer offset=%x\n",offset);return 31;}
  put(mesh+offset,0);assert(frozen.ValidateFrozen().ok());
 }
 put(mesh+0x1d0,root);assert(!frozen.ValidateFrozen().ok());put(mesh+0x1d0,std::uintptr_t{});
 put(mesh+0x65e,static_cast<unsigned char>(3));assert(!frozen.ValidateFrozen().ok());put(mesh+0x65e,static_cast<unsigned char>(2));
 put(type+0x480,b+0x2043341);assert(!frozen.ValidateFrozen().ok());put(type+0x480,b+0x2043340);
 put(mesh+0x20,actor);assert(!frozen.ValidateFrozen().ok());put(mesh+0x20,root);
 state.meshes[0].weak[1]=3;assert(!frozen.ValidateFrozen().ok());state.meshes[0].weak[1]=2;
 assert(frozen.ValidateFrozen().ok());
 // Every update reads current native consumers. No historical snapshot or
 // expected pose grants admission; a newly added reader must be detected.
 const auto battle=b+0x11000,slots=b+0x13000;
 put(battle+0x508,state.manager);put(state.manager+0x3f8,State::Array{slots,1,1});put(slots,root);
 Sc6ReplayGroundDebrisState::UpdateDiagnostic diagnostic;
 const auto update=[&]{return Sc6ReplayGroundDebrisState::ValidateUpdate(b,reinterpret_cast<void*>(battle),reinterpret_cast<void*>(world),diagnostic).ok();};
 const auto binds_before_update=State::weak_bind_calls;
 assert(update() && diagnostic.roots==1 && diagnostic.meshes==1);
 assert(diagnostic.children[0].component==mesh && diagnostic.children[0].root==root
     && diagnostic.children[0].root_actor==actor && diagnostic.children[0].component_index==2
     && diagnostic.children[0].component_serial==17418 && diagnostic.children[0].root_slot==0
     && diagnostic.children[0].ring_slot==0);
 if(State::weak_bind_calls!=binds_before_update){std::puts("ground update observation invoked weak serial assignment");return 36;}
 // Native141DB1200 calls child+460 when +48 bit40 is set, including
 // PhysicsOnly children with empty overlaps. Exercise the extracted production
 // ValidateUpdate -> mesh_consumers path; no fixture admission result changes.
 // This synthetic child is not a witness of the flag's value at A617.
 unsigned char volume_flags{};
 if(!State::read(mesh+0x48,volume_flags) || (volume_flags&0x40)
    || !update() || diagnostic.roots!=1 || diagnostic.meshes!=1) {
  std::puts("ground physics-volume clear-bit control was not admitted");return 49;
 }
 const auto collision_calls_before_volume=State::collision_calls;
 put(mesh+0x48,static_cast<unsigned char>(volume_flags|0x40));
 if(update()) {
  std::puts("ground physics-volume RED: clear-bit child admitted; changing only component+0x48 bit 0x40 still admitted");return 50;
 }
 if(std::strcmp(diagnostic.check,"update_child_consumers") || diagnostic.owner!=mesh
    || diagnostic.roots!=1 || diagnostic.meshes!=0
    || State::collision_calls!=collision_calls_before_volume
    || State::weak_bind_calls!=binds_before_update) {
  std::puts("ground physics-volume rejection did not occur at production child admission");return 51;
 }
 put(mesh+0x48,volume_flags);
 if(!update() || diagnostic.roots!=1 || diagnostic.meshes!=1) {
  std::puts("ground physics-volume clear-bit control did not regain admission");return 52;
 }
 for(auto object:{root,mesh,actor}) {
  const int index=object==root?1:object==mesh?2:3;
  put(object+0xc,-1);assert(!update());put(object+0xc,99);assert(!update());put(object+0xc,index);
  auto& item=RC::Unreal::FUObjectArray::items[index];item.valid=false;assert(!update());item.valid=true;
  item.object=nullptr;assert(!update());item.object=reinterpret_cast<RC::Unreal::UObject*>(object==actor?root:actor);assert(!update());
  item.object=reinterpret_cast<RC::Unreal::UObject*>(object);assert(update());
 }
 assert(State::weak_bind_calls==binds_before_update);
 // Controlled native GetWorld return, with production checking its dispatch
 // and returned identity before inspecting the navigation system.
 const auto navigation=b+0x14000;
 put(world+0xe8,navigation);put(navigation+0x238,b+0x15000);put(mesh+0x188,0x180007u);
 if(update()){std::puts("ground update admitted an active navigation transform consumer");return 37;}
 assert(!frozen.ValidateFrozen().ok());
 put(navigation+0x3fa,static_cast<unsigned char>(1));assert(update() && frozen.ValidateFrozen().ok());
 put(b+0x3499188+0x5e8,b+0x2d72f1);assert(!update());put(b+0x3499188+0x5e8,b+0x2d72f0);
 put(navigation+0x3fa,static_cast<unsigned char>(0));put(navigation+0x238,std::uintptr_t{});
 if(update()){std::puts("ground update admitted owner navigation work with no octree");return 39;}
 // The bit21-only route goes straight to141C5EFD0 and has the no-octree
 // early return. Bit20 instead permits owner dispatch before that guard.
 put(mesh+0x188,0x280007u);assert(update());put(navigation+0x238,b+0x15000);assert(!update());
 put(mesh+0x188,0x180007u);
 put(navigation+0x238,b+0x15000);put(world+0xe8,std::uintptr_t{});assert(update());
 put(b+0x3499188+0x138,b+0x1c204e1);assert(!update());put(b+0x3499188+0x138,b+0x1c204e0);
 put(mesh+0x188,0x80006u);assert(update());
 put(type+0x448,b+0x1dbe831);
 if(update()){std::puts("ground update admitted changed bounds transform dispatch");return 38;}
 assert(!frozen.ValidateFrozen().ok());put(type+0x448,b+0x1dbe830);assert(update());
 // Body-output publication calls actor+410. A null owner root uses the
 // native zero height, which can still dispatch a kill-height callback.
 put(actor+0x390,static_cast<unsigned char>(1));put(actor+0x394,1.0f);
 if(update()){std::puts("ground update admitted owner kill-height callback");return 40;}
 assert(!frozen.ValidateFrozen().ok());
 put(actor+0x394,-1.0f);assert(update() && frozen.ValidateFrozen().ok());
 const auto bounds_update_binds=State::weak_bind_calls;
 put(actor+0x394,0.0f);assert(update()); // Native comparison permits equality.
 put(b+0x418ac50,-2.0f);assert(!update());
 put(b+0x418ac50,2.0f);put(actor+0x394,1.0f);assert(update());
 for(auto nonfinite:{0x7f800000u,0xff800000u,0x7fc00000u}) {
  put(actor+0x394,nonfinite);assert(!update());put(actor+0x394,0.0f);
  put(b+0x418ac50,nonfinite);assert(!update());put(b+0x418ac50,0.0f);
 }
 put(actor+0x390,static_cast<unsigned char>(0));put(actor+0x394,0x7fc00000u);assert(update());
 // Selected native settings need their own indexed identity. This edge is
 // controlled; it does not grant success to the production predicate.
 settings_result=0;assert(!update());settings_result=b+0x16000;put(settings_result+0xc,99);assert(!update());
 settings_result=actor;assert(update());
 put(actor+0x168,root);assert(!update());put(actor+0x168,std::uintptr_t{});
 put(b+0x3499188+0x138,b+0x1c204e1);assert(!update());put(b+0x3499188+0x138,b+0x1c204e0);
 state.world=world+8;assert(!state.owner_transform_consumers(actor));state.world=world;
 // A streaming class lookup must not initialize a native class while this
 // read-only admission runs. Reject before calling the lookup edge.
 const auto streams=b+0x17000;
 for(auto invalid:{State::Array{streams,-1,1},State::Array{streams,2,1},State::Array{streams,0,4097},State::Array{0,1,1}}) {
  put(world+0x88,invalid);const auto calls=settings_calls;assert(!update() && settings_calls==calls);
 }
 put(world+0x88,State::Array{streams,1,1});put(streams,std::uintptr_t{});assert(update());
 put(streams,b+0x18000);const auto calls=settings_calls;assert(!update() && settings_calls==calls);
 put(b+0x43c1ec0,b+0x19000);assert(update());
 put(world+0x88,State::Array{});put(b+0x43c1ec0,std::uintptr_t{});assert(update());
 assert(State::weak_bind_calls==bounds_update_binds);
 // Native1420297F0 and substep142055C40 dispatch these collections
 // before scene simulation. Notification queues do not cover this route.
 assert(update());put(physics_scene+0x60,1);
 if(update()){std::puts("ground update admitted unowned physics-step callback");return 41;}
 assert(!frozen.ValidateFrozen().ok());
 put(physics_scene+0x60,0);assert(update());
 // Rejected native callback diagnostics must expose actual storage without
 // executing the callable or changing the collection.
 const auto callback_collection=physics_scene+0x10,callback_heap=b+0x1b000,callback_vtable=b+0x1c000;
 put(callback_collection+0x40,callback_heap);put(callback_collection+0x50,1);
 put(callback_heap+0x30,1);put(callback_heap,callback_vtable);
 put(callback_heap+8,std::uintptr_t{42});put(callback_heap+16,actor);put(callback_heap+24,b+0x2345);
 put(callback_vtable+0x68,b+0x3456);
 assert(!update());
 if(diagnostic.physics_count!=1 || diagnostic.physics_observed!=1 || !diagnostic.physics_complete
    || !diagnostic.physics_callbacks[0].readable || diagnostic.physics_callbacks[0].callable!=callback_heap
    || diagnostic.physics_callbacks[0].invoke!=b+0x3456 || diagnostic.physics_callbacks[0].words[2]!=actor) {
  std::puts("ground callback rejection lacks bounded native callable census");return 43;
 }
 assert(State::read<int>(callback_collection+0x50,diagnostic.physics_count) && diagnostic.physics_count==1);
 const auto callable_heap=b+0x1d000;
 const std::array<std::uintptr_t,4> payload{callback_vtable,91,actor,b+0x4567};put(callable_heap,payload);
 put(callback_heap+0x20,callable_heap);assert(!update() && diagnostic.physics_complete);
 assert(diagnostic.physics_callbacks[0].callable==callable_heap && diagnostic.physics_callbacks[0].words==payload);
 put(callback_heap+0x20,std::uintptr_t{1});assert(!update() && !diagnostic.physics_complete);
 put(callback_heap+0x20,std::uintptr_t{});
 for(int oversized:{-1,9,0x7fffffff}) {
  put(callback_collection+0x50,oversized);assert(!update() && !diagnostic.physics_complete && !diagnostic.physics_observed);
 }
 put(callback_collection+0x50,1);
 put(callback_heap+0x30,0);assert(!update() && diagnostic.physics_complete && !diagnostic.physics_callbacks[0].callable);
 // Native inline storage is a distinct path, not a granted callback edge.
 put(callback_collection+0x40,std::uintptr_t{});put(callback_collection,payload);put(callback_collection+0x30,1);
 assert(!update() && diagnostic.physics_complete && diagnostic.physics_callbacks[0].callable==callback_collection);
 put(callback_collection+0x40,std::uintptr_t{});put(callback_collection+0x50,0);assert(update());
 // Actual native vehicle registration: raw delegates in both collections,
 // a live FPhysScene -> manager entry and matching nonzero handles. Native
 // methods return before queries or callbacks only for exactly zero vehicles.
 const auto vehicle=b+0x20000,vehicle_map=b+0x40e90a0,vehicle_rows=b+0x23000;
 const auto install_vehicle=[&] {
  put(vehicle_map,State::Array{vehicle_rows,1,1});put(vehicle_map+0x10,1u);
  put(vehicle_map+0x28,1);put(vehicle_map+0x2c,128);put(vehicle_map+0x30,-1);
  put(vehicle_rows,physics_scene);put(vehicle_rows+8,vehicle);put(vehicle+0x10,0);
  put(b+0x3510a68+0x68,b+0x2017dc0);
  for(unsigned side=0;side<2;++side) {
   const auto collection=physics_scene+(side?0x80:0x10),callable=b+0x21000+side*0x1000;
   put(collection+0x20,callable);put(collection+0x30,3);put(collection+0x40,std::uintptr_t{});
   put(collection+0x50,1);put(collection+0x64,0);
   put(callable,b+0x3510a68);put(callable+8,vehicle);put(callable+0x10,b+(side?0x30fe7a0:0x30fc2b0));
   put(callable+0x20,std::uint64_t(101+side));put(vehicle+0x60+side*8,std::uint64_t(101+side));
  }
 };
 install_vehicle();
 if(!update()) {std::puts("ground update rejected verified inert native vehicle callbacks");return 44;}
 assert(frozen.ValidateFrozen().ok());
 for(int vehicles:{1,-1}) {put(vehicle+0x10,vehicles);assert(!update());}put(vehicle+0x10,0);
 for(unsigned side=0;side<2;++side) {
  const auto collection=physics_scene+(side?0x80:0x10),callable=b+0x21000+side*0x1000;
  put(collection+0x30,2);assert(!update());install_vehicle();
  put(collection+0x64,1);assert(!update());install_vehicle();
  put(callable,b+0x3510a69);assert(!update());install_vehicle();
  put(b+0x3510a68+0x68,b+0x2017dc1);assert(!update());install_vehicle();
  put(callable+8,vehicle+0x100);assert(!update());install_vehicle();
  put(callable+0x10,b+0x2d72f0);assert(!update());install_vehicle();
  put(callable+0x20,std::uint64_t{});assert(!update());install_vehicle();
  put(vehicle+0x60+side*8,std::uint64_t{901});assert(!update());install_vehicle();
 }
 put(vehicle_rows,physics_scene+8);assert(!update());install_vehicle();
 put(vehicle_rows+8,vehicle+8);assert(!update());install_vehicle();
 put(physics_scene+0xd0,0);assert(!update());install_vehicle();
 put(vehicle_map,State::Array{vehicle_rows,2,2});put(vehicle_map+0x10,3u);put(vehicle_map+0x28,2);
 put(vehicle_rows+0x18,physics_scene);put(vehicle_rows+0x20,vehicle);assert(!update());install_vehicle();
 // Sparse holes are valid when allocation bits and free counts agree.
 put(vehicle_map,State::Array{vehicle_rows,2,2});put(vehicle_map+0x10,2u);put(vehicle_map+0x28,2);put(vehicle_map+0x34,1);
 assert(update());put(vehicle_map+0x34,0);install_vehicle();
 const auto allocation_bits=b+0x24000;
 put(allocation_bits,1u);put(vehicle_map+0x20,allocation_bits);assert(update());
 put(vehicle_map+0x20,std::uintptr_t{});
 put(vehicle_map+8,65);assert(!update());install_vehicle();
 for(unsigned side=0;side<2;++side) {
  const auto collection=physics_scene+(side?0x80:0x10),entries=b+0x25000+side*0x1000;
  std::array<std::byte,0x40> entry{};assert(State::read(collection,entry));put(entries,entry);
  put(collection+0x40,entries);assert(update());install_vehicle();
 }
 put(vehicle_map+0x10,0u);put(vehicle_map+0x34,1);assert(!update());
 put(vehicle_map+0x34,0);install_vehicle();
 for(unsigned side=0;side<2;++side) {
  const auto collection=physics_scene+(side?0x80:0x10);
  put(collection+0x20,std::uintptr_t{});put(collection+0x30,0);put(collection+0x50,0);
 }
 put(vehicle_map,State::Array{});assert(update());
 for(unsigned offset:{0x60u,0xd0u,0x74u,0xe4u}) {
  for(int value:{1,-1}) {
   put(physics_scene+offset,value);assert(!update() && diagnostic.owner==physics_scene+offset);
   assert(std::strcmp(diagnostic.check,offset==0x60?"update_physics_step_callbacks":offset==0xd0?"update_physics_substep_callbacks":offset==0x74?"update_physics_step_recursion":"update_physics_substep_recursion")==0);
   assert(!frozen.ValidateFrozen().ok());put(physics_scene+offset,0);assert(update());
  }
 }
 put(state.manager+0x3f8,State::Array{slots,0,1});assert(update());
 put(physics_scene+0xd0,1);assert(!update());put(physics_scene+0xd0,0);assert(update());
 put(state.manager+0x3f8,State::Array{slots,1,1});
 put(world+0x1c8,std::uintptr_t{});assert(!update());put(world+0x1c8,physics_scene);assert(update());
 put(mesh+0x3b0,1);assert(!update() && diagnostic.owner==mesh);put(mesh+0x3b0,0);assert(update());
 put(root,b+0x33566f9);assert(!update());put(root,b+0x33566f8);assert(update());
 put(b+0x3356f68+0x600,b+0x89f5f1);assert(!update());put(b+0x3356f68+0x600,b+0x89f5f0);assert(update());
 put(root+0x830,static_cast<unsigned char>(3));
 for(auto callback:{0x89fae0u,0x89fe30u,0x89fc70u}) {
  put(controller+16,b+callback);assert(update());assert(!state.read_root(root,captured));
 }
 put(controller+16,b+0x89ff80);assert(!update());
 put(root+0x830,static_cast<unsigned char>(2));put(controller+16,b+0x8a0230);assert(update());
 put(state.manager+0x3f8,State::Array{slots,2,2});put(slots+0xc0,root);assert(!update());
 put(state.manager+0x3f8,State::Array{slots,1,1});assert(update());
 State::collision_admitted=false;
 if(update()) {std::puts("ground update ignored the current collision domain");return 46;}
 assert(State::collision_calls && diagnostic.owner==mesh && std::strcmp(diagnostic.check,"update_collision_domain")==0);
 State::collision_admitted=true;assert(update());
 // An empty historical ground inventory does not mean the world's callback
 // domain stayed empty while preparation waited for native/GPU completion.
 // Exercise the actual frozen-state predicate, including the real delegate
 // layout reader; no callback-admission stub grants this result.
 const auto saved_count=state.count;state.count=0;
 assert(frozen.ValidateFrozen().ok());
 for(unsigned offset:{0x60u,0xd0u,0x74u,0xe4u}) {
  for(int value:{1,-1}) {
   put(physics_scene+offset,value);
   if(frozen.ValidateFrozen().ok()) {
    std::printf("empty frozen ground ignored changed physics callback offset=%x value=%d\n",offset,value);
    return 48;
   }
   put(physics_scene+offset,0);assert(frozen.ValidateFrozen().ok());
  }
 }
 put(world+0x1c8,std::uintptr_t{});assert(!frozen.ValidateFrozen().ok());
 put(world+0x1c8,physics_scene);assert(frozen.ValidateFrozen().ok());
 state.count=saved_count;assert(frozen.ValidateFrozen().ok());
 // Configuration installation cannot replace a live actor or reuse its IDs.
 std::array<std::byte,0x230> fresh_body{};std::array<std::byte,16> fresh_responses{};
 const auto fresh=reinterpret_cast<std::uintptr_t>(fresh_body.data());
 const auto response_copy=reinterpret_cast<std::uintptr_t>(fresh_responses.data());
 const auto write=[](auto p,const auto& v){put(p,v);return true;};
 const auto read=[](auto p,auto& v){return State::read(p,v);};
 put(fresh+0x74,0x40000u);put(fresh+0x208,fresh);put(fresh+0x150,std::uint64_t{0x12345678});
 put(fresh+0xf0,std::uintptr_t{1});const auto before=fresh_body;
 assert(!captured_mesh.body_configuration.ApplyCold(read,write,fresh,response_copy)&&fresh_body==before);
 put(fresh+0xf0,std::uintptr_t{});
 // Duplication/profile initialization may already own a response allocation.
 // Replacing it must return that allocation to an explicit retirement owner.
 std::array<std::byte,32> default_responses{};
 const auto defaults=reinterpret_cast<std::uintptr_t>(default_responses.data());
 put(fresh+0x60,State::Array{defaults,1,2});
 std::uintptr_t displaced{};
 const auto with_defaults=fresh_body;
 assert(!captured_mesh.body_configuration.ApplyCold(read,write,fresh,response_copy));
 assert(fresh_body==with_defaults); // No retirement owner, no mutation.
 assert(!captured_mesh.body_configuration.ApplyCold(read,write,fresh,defaults,nullptr,nullptr,&displaced));
 assert(!displaced && fresh_body==with_defaults); // No aliasing the displaced allocation.
 assert(captured_mesh.body_configuration.ApplyCold(read,write,fresh,response_copy,nullptr,nullptr,&displaced));
 assert(displaced==defaults);
 const auto installed_body=fresh_body;
 assert(!captured_mesh.body_configuration.ApplyCold(read,write,fresh,response_copy,nullptr,nullptr,&displaced));
 assert(fresh_body==installed_body && displaced==defaults); // An existing retirement owner cannot be overwritten.
 unsigned installed_flags{};std::memcpy(&installed_flags,fresh_body.data()+0x74,4);assert(installed_flags==0x3fffff);
 std::uintptr_t self{};std::memcpy(&self,fresh_body.data()+0x208,8);assert(self==fresh);
 std::uint64_t callback{};std::memcpy(&callback,fresh_body.data()+0x150,8);assert(callback==0x12345678);
 State::Array installed_response{};std::memcpy(&installed_response,fresh_body.data()+0x60,sizeof(installed_response));
 assert(installed_response.data==response_copy && installed_response.data!=body_responses.data && installed_response.count==1);
 std::memcpy(&channel,fresh_responses.data(),8);assert(channel==73);
 // Owned child state must survive source mutation and deallocation. Reading a
 // source pointer later, or reusing the root pose, does not reconstruct it.
 put(mesh+0x270,transform);
 assert(!std::memcmp(captured_mesh.transform.data(),child_transform.data(),sizeof(child_transform)));
 assert(VirtualFree(memory,0,MEM_RELEASE));
 assert(captured_mesh.body_configuration.BuildNativeView(body_view));
 std::memcpy(&body_responses,body_view.data()+0x60,sizeof(body_responses));
 std::memcpy(&channel,reinterpret_cast<void*>(body_responses.data),8);assert(channel==73);
 assert(!std::memcmp(captured_mesh.transform.data(),child_transform.data(),sizeof(child_transform)));
}
